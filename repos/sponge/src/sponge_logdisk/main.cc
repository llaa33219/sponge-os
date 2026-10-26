/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_logdisk — persistent system log on the writable partition.
 *
 * The LOG sink that closes the field-diagnosis gap of
 * docs/17-runtime-component-topology.md §5: on the serial-less LG gram
 * a freeze leaves NOTHING to read, because the only durable record of
 * what the machine was doing lived in the LOG session stream — which
 * the vendored Genode core terminates on the serial line
 * (repos/base/src/core/include/log_session_component.h ->
 * Genode::log -> Core::Core_log -> Kernel::log -> serial UART).
 *
 * This component re-roots that stream onto the media:
 *
 *     component --LOG--> sponge_logdisk --File_system--> vfs_var (P4)
 *
 * The scenario routes the LOG requests of the whole system here
 * instead of to core, so init's own announcements and every child's
 * log() output land in one file on SPONGE-VAR. After a hard reset the
 * file is still there: that is the freeze evidence.
 *
 * Why a Sponge-owned component and not upstream's
 * server/terminal_log + gems/file_terminal pair (the shape used by
 * genode/repos/os/run/log_core.run):
 *
 *   1. SIZE CAP. Neither upstream piece caps the file. SPONGE-VAR is
 *      64 MiB and shares a filesystem with pkgd's installed-set store
 *      (docs/14 §4.3): an unbounded log is a filesystem-full event, and
 *      a full SPONGE-VAR takes the package manager down with it. This
 *      component keeps the current boot's <path> plus exactly ONE
 *      previous boot (<path><rotate_suffix>).
 *   2. PER-BOOT FILES. file_terminal opens with O_CREAT|O_RDWR at
 *      offset 0 and never truncates, so a second boot OVERWRITES the
 *      first boot's evidence from byte 0. For a freeze-forensics
 *      component that is the wrong default; here every boot moves the
 *      previous file aside and starts a fresh one.
 *   3. WRITE EAGERNESS UNDER WEDGE. This component waits for the
 *      acknowledgement of every drained batch (Vfs_handle::write
 *      followed by sync), so a log() call is at most one drain behind
 *      the media. It also emits a periodic tick line, so a freeze shows
 *      up as a GAP between two ticks rather than as an absent log.
 *   4. BACKLOG. Between construction and the first successful open
 *      (the File_system session resolves only once vfs_var has mounted
 *      ext2fs) messages are held in a fixed RAM backlog and flushed on
 *      open, so early-boot lines survive.
 *
 * === The two invariants that make this safe on the LOG path ===
 *
 * (A) THE LOG SERVICE MUST NEVER BLOCK ON THE FILESYSTEM.
 *
 *     Opening the log's File_system session is a BLOCKING session
 *     request: it returns only once vfs_var has mounted ext2fs. Issued
 *     from the thread that serves LOG writes, the first component to log
 *     would block inside log() until the whole storage chain came up —
 *     and report_rom, which every driver depends on for its reports,
 *     logs early. The result is a boot-time deadlock, which is exactly
 *     what the first prototype did (observed: the uefi-usb product
 *     media froze right after acpi finished parsing its tables, with
 *     report_rom stuck behind its own log call).
 *
 *     Hence the topology below: the LOG root is served on a SECONDARY
 *     entrypoint, so log writes are dispatched by that entrypoint's own
 *     RPC thread while the component's MAIN entrypoint thread blocks on
 *     the filesystem. Sessions only append to a RAM backlog there and
 *     wake the main entrypoint with a signal; every filesystem
 *     operation happens on the main entrypoint.
 *
 * (B) ALL FILESYSTEM I/O HAPPENS ON THE MAIN ENTRYPOINT.
 *
 *     Vfs_handle::write answers asynchronously; the Vfs library turns
 *     that into a synchronous call via commit_and_wait(), which
 *     dispatches signals on the component's Env entrypoint. Doing that
 *     from the root's entrypoint while the main entrypoint is blocked in
 *     a session request would deadlock. Sessions never enqueue I/O.
 *
 * Minimum privilege (AGENTS.md §1.2): a File_system session (vfs_var,
 * label "sponge_logdisk -> ", policy root /log) and the LOG sessions it serves.
 * A Timer session is opened ONLY when the config carries a non-zero
 * "tick_ms" attribute — without it there are no timestamps and no tick
 * line, and an unrouted Timer request would block construction
 * outright (a denied session request blocks in Genode).
 *
 * Config:
 *   <config path="/boot.log" max_bytes="8388608" rotate_suffix=".1"
 *           tick_ms="5000" tick_log="no">
 *     <vfs> <fs name="log"/> </vfs>               (optional; without
 *     it the sink still serves LOG sessions but buffers and drops —
 *     the honest degradation, and no recursion into a log sink that
 *     cannot log)
 *   </config>
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <base/mutex.h>
#include <base/service.h>
#include <log_session/log_session.h>
#include <root/component.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <vfs/root.h>
#include <vfs/vfs_handle.h>

namespace Sponge {
	struct Logdisk;
	struct Session;
	struct Root;

	enum {
		/* Longest single output line, after prefixing. */
		LINE_MAX  = 1024,
		LABEL_LEN = 96,
		PATH_LEN  = 128,
	};

	/* Bounded string append (no libc, no format engine). */
	inline void append(char *buf, Genode::size_t &n, Genode::size_t cap,
	                   char const *s)
	{
		while (*s && n + 1 < cap) buf[n++] = *s++;
		buf[n] = 0;
	}

	inline void append(char *buf, Genode::size_t &n, Genode::size_t cap,
	                   char c)
	{
		if (n + 1 < cap) buf[n++] = c;
		buf[n] = 0;
	}

	inline void append_num(char *buf, Genode::size_t &n, Genode::size_t cap,
	                       Genode::uint64_t v)
	{
		char tmp[24];
		unsigned i = 0;
		do { tmp[i++] = char('0' + (v % 10)); v /= 10; } while (v && i < sizeof(tmp));
		while (i) {
			if (n + 1 >= cap) { n = cap - 1; buf[n] = 0; return; }
			buf[n++] = tmp[--i];
		}
		buf[n] = 0;
	}

	/* Defined after 'Logdisk' (it uses its members). */
	void log_line(Logdisk &, char const *prefix, char const *msg);
}


/*
 * One LOG session. Dispatched on the root entrypoint's RPC thread and
 * doing nothing but formatting into the RAM backlog — see invariant (A).
 */
struct Sponge::Session : Genode::Rpc_object<Genode::Log_session>
{
	Logdisk *_logdisk;
	char     _prefix[LABEL_LEN];

	Session(Session const &) = delete;
	Session &operator=(Session const &) = delete;

	Session(Logdisk *ld, char const *label) : _logdisk(ld)
	{
		Genode::size_t n = 0;
		Sponge::append(_prefix, n, LABEL_LEN, '[');
		Sponge::append(_prefix, n, LABEL_LEN, label);
		Sponge::append(_prefix, n, LABEL_LEN, "] ");
	}

	void write(Genode::Log_session::String const &string_buf) override
	{
		if (!string_buf.valid_string()) {
			Genode::warning("sponge_logdisk: corrupted log string");
			return;
		}
		Sponge::log_line(*_logdisk, _prefix, string_buf.string());
	}
};


struct Sponge::Root : Genode::Root_component<Session>
{
	Logdisk *_logdisk { nullptr };

	Root(Root const &) = delete;
	Root &operator=(Root const &) = delete;

	Root(Genode::Entrypoint &ep, Genode::Sliced_heap &md_alloc)
	: Genode::Root_component<Session>(ep, md_alloc) { }

	Create_result _create_session(char const *args) override
	{
		char label_buf[LABEL_LEN];
		Genode::Arg const label_arg = Genode::Arg_string::find_arg(args, "label");
		label_arg.string(label_buf, sizeof(label_buf), "");
		return *new (md_alloc()) Session { _logdisk, label_buf };
	}
};


struct Sponge::Logdisk : Genode::Noncopyable
{
	Logdisk(Logdisk const &) = delete;
	Logdisk &operator=(Logdisk const &) = delete;

	enum {
		/* RAM backlog: ~2600 lines of 100 bytes. Bounds both the
		 * "filesystem not yet available" window (early boot) and the
		 * pathological "filesystem stopped answering" window without
		 * ever exhausting the child's RAM quota. */
		PENDING_MAX = 256*1024,
	};

	Genode::Env &_env;

	Genode::Heap        _heap     { _env.ram(), _env.rm() };
	Genode::Sliced_heap _md_alloc { _env.ram(), _env.rm() };

	/* The LOG service runs on a SECONDARY entrypoint so that the main
	 * entrypoint is free to block on the File_system session request —
	 * see invariant (A). The public Entrypoint constructor starts the
	 * entrypoint's own signal-proxy and RPC threads and returns. */
	Genode::Entrypoint _root_ep { _env, 16*1024, "logsink",
	                              Genode::Affinity::Location { } };
	Root                _root   { _root_ep, _md_alloc };

	/* Constructed only when <config> carries a <vfs> node. */
	Genode::Constructible<Genode::Vfs::Root> _vfs { };

	/* Constructed only when "tick_ms" is non-zero. */
	Genode::Constructible<Timer::Connection> _timer { };

	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	/* ---------------- configuration ---------------- */

	char             _path[PATH_LEN]    { "/boot.log" };
	char             _rotate_suffix[32] { ".1" };
	Genode::uint64_t _max_bytes         { 8ull*1024*1024 };
	Genode::uint64_t _tick_ms           { 0 };
	bool             _tick_log          { false };

	/* ---------------- pending buffer ----------------
	 *
	 * Sessions (root entrypoint) append under _lock and wake the main
	 * entrypoint. The main entrypoint drains. Every critical section is
	 * short and holds no filesystem operation.
	 */
	Genode::Mutex    _lock { };
	char             _pending[PENDING_MAX];
	Genode::size_t   _pending_len { 0 };
	Genode::size_t   _pending_off { 0 };
	bool             _tick_pending { false };
	bool             _io_active { false };
	Genode::uint64_t _dropped { 0 };

	Genode::Signal_receiver           _work_recv { };
	Genode::Signal_context            _work_ctx  { };
	Genode::Signal_context_capability _work_cap  { _work_recv.manage(_work_ctx) };

	/* ---------------- file state (main entrypoint only) --------------- */

	Genode::Vfs::Vfs_handle *_file       { nullptr };
	Genode::uint64_t         _file_bytes { 0 };
	Genode::uint64_t         _ticks      { 0 };
	Genode::uint64_t         _ms         { 0 };

	void handle_work();
	void handle_tick();

	Genode::Signal_handler<Logdisk> _work_sig {
		_env.ep(), *this, &Logdisk::handle_work };
	Genode::Signal_handler<Logdisk> _tick_sig {
		_env.ep(), *this, &Logdisk::handle_tick };

	/* ---------------- configuration ---------------- */

	/* Copy a raw attribute value out of the node. Returns false when the
	 * attribute is absent. Node::attribute_value() only parses NUMBERS in
	 * this Genode version, so string attributes are read raw. */
	static bool _attr(Genode::Node const &node, char const *name,
	                  char *dst, Genode::size_t cap)
	{
		bool found { false };
		node.for_each_attribute([&] (Genode::Node::Attribute const &attr) {
			if (found || !(attr.name == name))
				return;
			Genode::size_t const n = attr.value.num_bytes < (cap - 1)
			                      ? attr.value.num_bytes : (cap - 1);
			Genode::memcpy(dst, attr.value.start, n);
			dst[n] = 0;
			found = true;
		});
		return found;
	}

	void _read_config()
	{
		_config_rom.update();
		if (!_config_rom.valid())
			return;

		Genode::Node const node = _config_rom.node();

		_attr(node, "path", _path, sizeof(_path));
		_attr(node, "rotate_suffix", _rotate_suffix, sizeof(_rotate_suffix));

		Genode::uint64_t const max = node.attribute_value("max_bytes", 0ull);
		if (max > 0) _max_bytes = max;

		_tick_ms = node.attribute_value("tick_ms", 0u);

		/* 'tick_log: yes' echoes each tick line onto the component's OWN
		 * log session — which the scenario routes to core (the serial
		 * line), not to this sink. It gives a headless run scenario
		 * something to gate on while the LOG stream itself goes to
		 * disk. The product media leaves it off: on the serial-less gram
		 * there is no reader, and the file is the record. */
		_tick_log = node.attribute_value("tick_log", false);
	}

	/* ---------------- pending buffer ---------------- */

	/* Append one formatted line. _lock held by the caller. */
	void _append_locked(char const *s, Genode::size_t len)
	{
		if (_pending_len + len > PENDING_MAX) {

			if (_pending_off == 0 && len > PENDING_MAX)
				return;  /* a single line longer than the whole backlog */

			/* Backlog full: drop the OLDEST bytes. The most recent
			 * lines are the freeze prelude, so they win. */
			Genode::size_t const excess = _pending_len + len - PENDING_MAX;
			_pending_off += excess;
			_pending_len -= excess;
			_dropped     += excess;

			if (_pending_off >= PENDING_MAX/2) {
				Genode::size_t const rest = _pending_len - _pending_off;
				Genode::memmove(_pending, _pending + _pending_off, rest);
				_pending_len = rest;
				_pending_off = 0;
			}
			if (_pending_len + len > PENDING_MAX)
				return;
		}
		Genode::memcpy(_pending + _pending_len, s, len);
		_pending_len += len;
	}

	/* ---------------- file output (main entrypoint only) -------------- */

	void _commit()
	{
		if (_vfs.constructed())
			_vfs->io().commit_and_wait();
	}

	void _close_file()
	{
		if (!_file)
			return;
		_file->close();          /* closes AND frees the handle */
		_file       = nullptr;
		_file_bytes = 0;
	}

	/* Move <path> to <path><rotate_suffix> if it exists and is non-empty.
	 * Returns true if a file was moved. */
	bool _rotate_existing()
	{
		if (!_vfs.constructed())
			return false;

		Genode::Vfs::File_system &vfs = _vfs->fs();

		Genode::Vfs::Directory_service::Stat stat { };
		if (vfs.stat(_path, stat) != Genode::Vfs::Directory_service::STAT_OK)
			return false;

		if (stat.size == 0)
			return false;

		char rotated[PATH_LEN+32];
		Genode::size_t rn = 0;
		Sponge::append(rotated, rn, sizeof(rotated), _path);
		Sponge::append(rotated, rn, sizeof(rotated), _rotate_suffix);

		if (vfs.rename(_path, rotated)
		    == Genode::Vfs::Directory_service::RENAME_OK) {
			/* Announce the preserved evidence. This line goes to the
			 * component's OWN log session, which the scenario routes
			 * to core (serial) — a headless run scenario can gate on
			 * it, and the notice is also the first line of the new
			 * boot's own log for whoever reads the medium by hand. */
			Genode::log("sponge_logdisk: previous boot log preserved as ",
			            Genode::String<PATH_LEN+32>(rotated));
			return true;
		}

		/* No rename support on this filesystem: fall back to truncating
		 * in place. The cap still holds; the previous boot's bytes do
		 * not survive this fallback (documented in docs/evidence/). */
		Genode::Vfs::Vfs_handle *h { nullptr };
		if (vfs.open(_path, Genode::Vfs::Directory_service::OPEN_MODE_WRONLY,
		             &h, _heap) == Genode::Vfs::Directory_service::OPEN_OK) {
			Genode::Vfs::Vfs_handle::Guard guard(h);
			h->ftruncate(0);
		}
		Genode::warning("sponge_logdisk: rotate rename unsupported — the previous "
		                "boot's log was truncated in place");
		return false;
	}

	bool _open()
	{
		if (!_vfs.constructed() || _file)
			return _file != nullptr;

		Genode::Vfs::File_system &vfs = _vfs->fs();

		Genode::Vfs::Vfs_handle *handle { nullptr };
		if (vfs.open(_path,
		             Genode::Vfs::Directory_service::OPEN_MODE_WRONLY
		             | Genode::Vfs::Directory_service::OPEN_MODE_CREATE,
		             &handle, _heap) != Genode::Vfs::Directory_service::OPEN_OK)
			return false;

		/* Append: continue at the current end of the file. */
		Genode::Vfs::Directory_service::Stat stat { };
		_file_bytes = (vfs.stat(_path, stat)
		               == Genode::Vfs::Directory_service::STAT_OK)
		            ? stat.size : 0;
		_file = handle;
		return true;
	}

	void _emit(char const *data, Genode::size_t len)
	{
		if (!_file || !len)
			return;

		if (_file_bytes + len > _max_bytes) {
			_close_file();
			_rotate_existing();
			if (!_open())
				return;  /* rotation failed: drop, never wedge the loggers */
		}

		Genode::size_t off { 0 };
		while (off < len) {
			Genode::Vfs::Vfs_handle::Write_result w
				{ Genode::Vfs::Vfs_handle::Write_error::RETRY };
			for (;;) {
				w = _file->write(Genode::Vfs::At { .pos = _file_bytes + off },
				                 Genode::Const_byte_range_ptr(data + off,
				                                               len - off));
				if (w != Genode::Vfs::Vfs_handle::Write_error::RETRY)
					break;
				_commit();
			}
			Genode::size_t const n = w.convert<Genode::size_t>(
				[](Genode::size_t bytes) { return bytes; },
				[](Genode::Vfs::Vfs_handle::Write_error) { return Genode::size_t(0); });

			if (n == 0) {
				/* The filesystem is gone (full, unmounted, denied).
				 * Disable persistence rather than wedging every logger
				 * in the system on a dead session. */
				Genode::warning("sponge_logdisk: write failed — persistence "
				                "disabled");
				_close_file();
				return;
			}
			off         += n;
			_file_bytes += n;
		}

		while (_file->sync() == Genode::Vfs::Sync_result::RETRY)
			_commit();
	}

	void _boot_header()
	{
		char line[LINE_MAX];
		Genode::size_t n = 0;
		Sponge::append(line, n, LINE_MAX, "=== sponge_logdisk: boot start, path ");
		Sponge::append(line, n, LINE_MAX, _path);
		Sponge::append(line, n, LINE_MAX, ", cap ");
		Sponge::append_num(line, n, LINE_MAX, _max_bytes);
		Sponge::append(line, n, LINE_MAX, " bytes ===\n");
		_emit(line, n);
	}

	void _tick_line()
	{
		char line[LINE_MAX];
		Genode::size_t n = 0;
		Sponge::append_num(line, n, LINE_MAX, ++_ticks);
		Sponge::append(line, n, LINE_MAX, " [sponge_logdisk] tick alive dropped=");
		Sponge::append_num(line, n, LINE_MAX, _dropped);
		Sponge::append(line, n, LINE_MAX, '\n');
		_emit(line, n);

		if (_tick_log)
			Genode::log("sponge_logdisk: tick alive, dropped ", _dropped);
	}

	/* ---------------- drain (main entrypoint) ---------------- */

	void _drain()
	{
		{
			Genode::Mutex::Guard g { _lock };
			if (_io_active)
				return;  /* re-entrant: the outer loop picks the rest up */
			_io_active = true;
		}

		for (;;) {
			Genode::size_t off { 0 }, len { 0 };
			bool tick { false };

			{
				Genode::Mutex::Guard g { _lock };
				if (_pending_off == _pending_len && !_tick_pending) {
					_io_active = false;
					return;
				}
				off  = _pending_off;
				len  = _pending_len - _pending_off;
				tick = _tick_pending;
				_tick_pending = false;
			}

			/* Outside the lock: _emit() dispatches signals. Concurrent
			 * appends only touch [pending_len, ...), so the
			 * [off, pending_len) range read here stays stable. */
			if (_file && len)
				_emit(_pending + off, len);

			{
				Genode::Mutex::Guard g { _lock };
				_pending_off = 0;
				_pending_len = 0;
			}

			if (tick)
				_tick_line();
		}
	}

	/*
	 * Monotonic milliseconds, sampled on the MAIN entrypoint only.
	 *
	 * Taking the clock on the session (root entrypoint) thread would
	 * nest a Timer RPC inside a LOG RPC across two entrypoints, which
	 * can block the root thread until the main entrypoint's signal proxy
	 * runs — i.e. a LOG write could stall the whole logging path. The
	 * tick signal refreshes the value every tick_ms (2-5 s), which is
	 * more than enough resolution for "where did the freeze happen".
	 */
	Genode::uint64_t _now_ms() { return _ms; }

	/* Format one incoming message (possibly multi-line) and queue it.
	 * Runs on the root entrypoint — RAM only, never touches the
	 * filesystem. */
	void log_line(char const *prefix, char const *msg)
	{
		Genode::uint64_t const ms = _now_ms();

		char const *p = msg;
		while (*p) {
			char line[LINE_MAX];
			Genode::size_t n = 0;

			Sponge::append_num(line, n, LINE_MAX, ms);
			Sponge::append(line, n, LINE_MAX, ' ');
			Sponge::append(line, n, LINE_MAX, prefix);

			while (*p && *p != '\n') {
				if (*p == 27) {
					/* ESC ... m — a colour marker, not text. Drop it so
					 * the file stays plain text and greppable. */
					p++;
					while (*p && *p != 'm') p++;
					if (*p) p++;
					continue;
				}
				/* char overload on purpose: the string overload would
				 * append the whole remaining message on every
				 * character. */
				Sponge::append(line, n, LINE_MAX, *p);
				p++;
			}
			if (*p == '\n') p++;
			Sponge::append(line, n, LINE_MAX, '\n');

			{
				Genode::Mutex::Guard g { _lock };
				_append_locked(line, n);
			}

		}

		Genode::Signal_transmitter(_work_cap).submit();
	}

	/* ---------------- construction ---------------- */

	/*
	 * Constructor order is load-bearing.
	 *
	 * Genode's 'Child' creates a LOG environment session for EVERY child
	 * before the child is started (base/include/base/child.h:628), and
	 * init refuses to start a child whose env is incomplete
	 * (os/src/lib/sandbox/child.cc:40-49). So this component announces
	 * its LOG service before anything else — including the File_system
	 * request below, which blocks until vfs_var has mounted ext2fs,
	 * while vfs_var's own start waits for this component's LOG service.
	 * Announcing first breaks that otherwise circular bootstrap, and the
	 * secondary entrypoint keeps loggers moving meanwhile.
	 *
	 * The root is announced via ITS OWN entrypoint, not env.ep(). init
	 * creates a LOG session by invoking the root's 'create' RPC, and that
	 * RPC is dispatched on the entrypoint the root is managed on — on
	 * env.ep() it would run on the very thread blocked in the
	 * File_system request below, so the first child to ask for a LOG
	 * session would hang there for the whole boot (mirrors
	 * genode/repos/os/src/test/event_filter/main.cc:215).
	 */
	Logdisk(Genode::Env &env) : _env(env)
	{
		_read_config();

		_root._logdisk = this;
		_env.parent().announce(_root_ep.manage(_root));

		try {
			_config_rom.node().with_optional_sub_node("vfs",
				[&] (Genode::Node const &vfs_node) {
					_vfs.construct(_env, _heap, vfs_node);
				});
		} catch (Genode::Xml_node::Invalid_syntax) {
			Genode::warning("sponge_logdisk: malformed <config>");
		}

		if (_vfs.constructed()) {
			/* Move a surviving file aside BEFORE opening: this boot's
			 * log must never overwrite the previous boot's evidence. */
			_rotate_existing();
			if (_open()) {
				_boot_header();
				Genode::log("sponge_logdisk: logging to ",
				            Genode::String<PATH_LEN>(_path),
				            " (cap ", _max_bytes, " bytes)");
			} else
				Genode::warning("sponge_logdisk: cannot open the log file on the "
				                "writable partition — buffering only");
		} else {
			Genode::warning("sponge_logdisk: no <vfs> node — LOG sessions are "
			                "served but nothing is persisted");
		}

		if (_tick_ms) {
			try {
				_timer.construct(_env);
			} catch (...) {
				Genode::warning("sponge_logdisk: no Timer session — no tick flush");
			}
			if (_timer.constructed()) {
				/*
				 * Do not call 'curr_time' on this connection: it would
				 * switch the connection into the timeout-framework mode,
				 * which rejects the custom signal handler below. The
				 * timestamps are boot-relative, advanced by each tick.
				 */
				_timer->sigh(_tick_sig);
				_timer->trigger_periodic(_tick_ms*1000);
			}
		}

		/* Flush whatever accumulated while the filesystem was coming up. */
		Genode::Signal_transmitter(_work_cap).submit();
	}
};


void Sponge::Logdisk::handle_work() { _drain(); }


void Sponge::Logdisk::handle_tick()
{
	{
		Genode::Mutex::Guard g { _lock };
		_tick_pending = true;
		_ms += _tick_ms;
	}
	_drain();
}


void Sponge::log_line(Logdisk &ld, char const *prefix, char const *msg)
{
	ld.log_line(prefix, msg);
}


void Component::construct(Genode::Env &env)
{
	static Sponge::Logdisk main { env };
}
