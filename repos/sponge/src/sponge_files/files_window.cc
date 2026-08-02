/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of Files_window. See files_window.h for the design.
 *
 * The component reads directories and files through the libc POSIX API
 * (opendir/readdir/open/read). The libc integration mounts the `<vfs>`
 * node from the metadata <config> — so /demo (tar, read-only) and
 * /writable (ram, read-write) appear as ordinary directories. Errors
 * are surfaced in three places at once (AGENTS.md §5.1: prove the
 * feature in code): the status label (UI), Genode::log (log), and the
 * `files` report's last_action/result (testability).
 */

#include "files_window.h"
#include "theme/theme_loader.h"

#include <base/log.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Sponge::Sponge_Files;


namespace {

/* Maximum number of bytes of a file's first lines shown in the preview
 * pane. Generous enough to verify the "open" feature, bounded so the
 * libc heap never has to grow for a pathological fixture. */
Genode::size_t const PREVIEW_BYTES = 1024;

/* Poll period for the request ROM — same value sponge-de's
 * ThemeController uses, picked for reliable detection without burning
 * CPU. Bounded per the hung_or_long_commands class. */
int const REQUEST_POLL_MS = 200;


/* Convert a libc errno to a short result string for the report. */
char const *errno_to_result(int e)
{
	switch (e) {
	case EROFS: case EACCES:             return "refused";
	case ENOENT:                         return "no-such-file";
	case EEXIST:                         return "already-exists";
	case EISDIR:                         return "is-directory";
	case ENOSPC:                         return "no-space";
	default:                             return "io-error";
	}
}

} /* namespace */


Files_window::Files_window(Genode::Env &env, Theme::Theme const &theme,
                           QWidget *parent)
:
	QWidget(parent),
	_env(env)
{
	setWindowTitle("Sponge Files");

	/*
	 * Place the window at the nitpicker domain origin. The run scenario
	 * routes this window's Gui session into a fixed "files" domain whose
	 * origin matches (0, 0), so the probe's pixel coordinates align with
	 * Qt widget-local coords (same scheme as pkg_gui_demo). 800x600
	 * comfortably fits the list + preview + status layout under seL4's
	 * 1024x768 panorama.
	 */
	setGeometry(0, 0, 800, 600);

	_apply_style(theme);

	/* ---- Top bar: path label + Up button ---- */
	auto *top = new QHBoxLayout();
	_path_label = new QLabel("/", this);
	_path_label->setObjectName("path");
	QFont path_font = _path_label->font();
	path_font.setBold(true);
	path_font.setPointSize(11);
	_path_label->setFont(path_font);

	_up_btn = new QPushButton("Up", this);
	_up_btn->setObjectName("up");
	connect(_up_btn, &QPushButton::clicked, this, &Files_window::on_up_clicked);

	top->addWidget(_path_label, 1);
	top->addWidget(_up_btn, 0);

	/* ---- Middle: list (left) + preview (right) ---- */
	_list = new QListWidget(this);
	_list->setObjectName("list");
	_list->setIconSize(QSize(16, 16));
	/*
	 * The probe double-clicks at a known list-relative coordinate to
	 * navigate. Reserve a generous left strip so the click always lands
	 * inside the first row regardless of style padding.
	 */
	_list->setMinimumWidth(380);
	connect(_list, &QListWidget::itemDoubleClicked,
	        this, &Files_window::on_item_double_clicked);

	_preview = new QTextEdit(this);
	_preview->setReadOnly(true);
	_preview->setPlaceholderText("(select a file to preview)");
	_preview->setMinimumWidth(380);

	auto *middle = new QHBoxLayout();
	middle->addWidget(_list, 1);
	middle->addWidget(_preview, 1);

	/* ---- Action bar: Copy + Delete + status ---- */
	_copy_btn = new QPushButton("Copy to /writable", this);
	_copy_btn->setObjectName("copy");
	connect(_copy_btn, &QPushButton::clicked,
	        this, &Files_window::on_copy_clicked);

	_delete_btn = new QPushButton("Delete", this);
	_delete_btn->setObjectName("delete");
	connect(_delete_btn, &QPushButton::clicked,
	        this, &Files_window::on_delete_clicked);

	_status_label = new QLabel("ready", this);
	_status_label->setObjectName("status");

	auto *actions = new QHBoxLayout();
	actions->addWidget(_copy_btn);
	actions->addWidget(_delete_btn);
	actions->addWidget(_status_label, 1);

	/* ---- Outer layout ---- */
	auto *root = new QVBoxLayout(this);
	root->addLayout(top);
	root->addLayout(middle, 1);
	root->addLayout(actions);

	/*
	 * Initial listing. The run scenario mounts /demo (read-only) and
	 * /writable (RAM) under the root; "/" lists both.
	 */
	_refresh();

	/*
	 * Report publisher — drives the probe. Enabled before any operation
	 * so the initial state (path="/", entries=N) appears at the probe
	 * before it issues a request.
	 */
	_report.enabled(true);
	_publish("init", "ok");

	/*
	 * Request-channel poll. The probe writes <request op="..." .../> to
	 * the files_request ROM (relayed by report_rom); the QTimer pulls
	 * and dispatches. Bounded per the hung_or_long_commands class.
	 */
	_poll_timer = new QTimer(this);
	_poll_timer->start(REQUEST_POLL_MS);
	QObject::connect(_poll_timer, &QTimer::timeout,
	                 this, &Files_window::_poll_request);

	Genode::log("sponge_files: window shown at /");
}


void Files_window::restyle(Theme::Theme const &theme)
{
	_apply_style(theme);
	update();
}


void Files_window::_apply_style(Theme::Theme const &theme)
{
	/*
	 * All visual values come from the theme (AGENTS.md §3.4 — no
	 * hardcoded colors). The accent button color and the alternating
	 * list row give the probe a non-trivial pixel scene to verify.
	 *
	 * QString::arg is chained one-arg-per-call to avoid the multi-arg
	 * overload-resolution ambiguity that mixed-type argument lists
	 * trigger on Qt 6.8 (the variadic arg() does not accept
	 * (unsigned, QString, QString, QString)).
	 */
	QString const window_bg = QString::asprintf("#%06x",
	                                            theme.window_bg().raw);
	QString const title     = QString::asprintf("#%06x",
	                                            theme.title_text().raw);
	QString const accent    = QString::asprintf("#%06x",
	                                            theme.accent().raw);
	QString const list_alt  = QString::asprintf("#%06x",
	                                            theme.list_alt().raw);
	QString const border    = QString::asprintf("#%06x",
	                                            theme.window_border().raw);
	QString const success_c = QString::asprintf("#%06x",
	                                            theme.success().raw);
	QString const radius    = QString::number(theme.border_radius());
	QString const bwidth    = QString::number(theme.border_width());

	setStyleSheet(QStringLiteral(
		"QWidget { background-color: %1; color: %2; }"
		"QPushButton { background-color: %3; color: %1;"
		"            border-radius: %4px; padding: 6px 12px;"
		"            border: %5px solid %6; }"
		"QPushButton:pressed { background-color: %2; color: %1; }"
		"QListWidget { background-color: %1; color: %2;"
		"             alternate-background-color: %7;"
		"             border: %5px solid %6; border-radius: %4px; }"
		"QListWidget::item { padding: 4px 6px; min-height: 20px; }"
		"QTextEdit { background-color: %1; color: %2;"
		"            border: %5px solid %6; border-radius: %4px; }"
		"QLabel#status { color: %8; }")
		.arg(window_bg)
		.arg(title)
		.arg(accent)
		.arg(radius)
		.arg(bwidth)
		.arg(border)
		.arg(list_alt)
		.arg(success_c));
}


/* =================== Navigation =================== */


void Files_window::on_up_clicked()
{
	if (_cwd == "/" || _cwd.isEmpty()) {
		_status("already at root");
		_publish("up", "refused");
		return;
	}
	_navigate("..");
}


void Files_window::on_item_double_clicked(QListWidgetItem *item)
{
	if (!item) return;

	/* Strip displayed "/" so the resolved path is /demo not /demo/. */
	QString name = item->text();
	if (name.size() > 1 && name.endsWith('/'))
		name.chop(1);

	QString const target = _resolve(name);
	QString const type = item->data(Qt::UserRole).toString();

	if (type == "dir") {
		_navigate(name);
	} else {
		_open(target);
	}
}


void Files_window::_navigate(QString target)
{
	QString const next = (target == "..") ? _resolve("..") : _resolve(target);
	if (next.isEmpty()) {
		_status("navigate: empty path");
		_publish("navigate", "refused");
		return;
	}

	/* Sanity-check that next is a directory. */
	QByteArray const n = next.toUtf8();
	struct stat st;
	if (::stat(n.constData(), &st) != 0) {
		int const e = errno;
		QString const msg = QStringLiteral("navigate %1: %2")
			.arg(next).arg(std::strerror(e));
		_status(msg);
		Genode::log("sponge_files: ", msg.toUtf8().constData());
		_publish("navigate", errno_to_result(e));
		return;
	}
	if (!S_ISDIR(st.st_mode)) {
		_status("navigate: not a directory");
		_publish("navigate", "refused");
		return;
	}

	_cwd = next;
	_path_label->setText(_cwd);
	_preview->clear();
	_refresh();
	_status("navigate " + _cwd);
	Genode::log("sponge_files: navigate -> ", _cwd.toUtf8().constData());
	_publish("navigate", "ok");
}


/* =================== Open / preview =================== */


void Files_window::_open(QString path)
{
	QByteArray const p = path.toUtf8();
	int const fd = ::open(p.constData(), O_RDONLY);
	if (fd < 0) {
		int const e = errno;
		QString const msg = QStringLiteral("open %1: %2")
			.arg(path).arg(std::strerror(e));
		_status(msg);
		Genode::log("sponge_files: ", msg.toUtf8().constData());
		_publish("open", errno_to_result(e));
		return;
	}

	char buf[PREVIEW_BYTES + 1];
	ssize_t const n = ::read(fd, buf, PREVIEW_BYTES);
	::close(fd);
	if (n < 0) {
		int const e = errno;
		QString const msg = QStringLiteral("read %1: %2")
			.arg(path).arg(std::strerror(e));
		_status(msg);
		Genode::log("sponge_files: ", msg.toUtf8().constData());
		_publish("open", errno_to_result(e));
		return;
	}
	buf[n] = '\0';

	_preview->setPlainText(QString::fromUtf8(buf));
	QString const msg = QStringLiteral("open %1 (%2 bytes)").arg(path).arg(n);
	_status(msg);
	Genode::log("sponge_files: ", msg.toUtf8().constData());
	_publish("open", "ok");
}


/* =================== Copy / delete (writable area only) =================== */


bool Files_window::_is_writable(QString const &path) const
{
	return path.startsWith("/writable/");
}


void Files_window::on_copy_clicked()
{
	QListWidgetItem *const item = _list->currentItem();
	if (!item) {
		_status("copy: no selection");
		_publish("copy", "refused");
		return;
	}
	QString const name = item->text();
	QString const src = _resolve(name);
	QString const dst = "/writable/" + name;

	if (!_is_writable(dst)) {
		_status("copy: destination must be inside /writable");
		_publish("copy", "refused");
		return;
	}
	_copy(src, dst);
}


void Files_window::_copy(QString src, QString dst)
{
	QByteArray const s = src.toUtf8();
	QByteArray const d = dst.toUtf8();

	int const fin = ::open(s.constData(), O_RDONLY);
	if (fin < 0) {
		int const e = errno;
		QString const msg = QStringLiteral("copy: open src %1: %2")
			.arg(src).arg(std::strerror(e));
		_status(msg);
		Genode::log("sponge_files: ", msg.toUtf8().constData());
		_publish("copy", errno_to_result(e));
		return;
	}

	int const fout = ::open(d.constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fout < 0) {
		int const e = errno;
		::close(fin);
		QString const msg = QStringLiteral("copy: open dst %1: %2")
			.arg(dst).arg(std::strerror(e));
		_status(msg);
		Genode::log("sponge_files: ", msg.toUtf8().constData());
		/*
		 * The headline Alpha-scope acceptance: deleting/copying INTO a
		 * read-only area is refused. log+report+UI all surface it so the
		 * probe can assert it without parsing pixels.
		 */
		_publish("copy", errno_to_result(e));
		return;
	}

	char buf[4096];
	ssize_t total = 0;
	for (;;) {
		ssize_t const r = ::read(fin, buf, sizeof(buf));
		if (r == 0) break;
		if (r < 0) {
			int const e = errno;
			::close(fin);
			::close(fout);
			::unlink(d.constData());   /* do not leave a partial copy */
			QString const msg = QStringLiteral("copy: read %1: %2")
				.arg(src).arg(std::strerror(e));
			_status(msg);
			Genode::log("sponge_files: ", msg.toUtf8().constData());
			_publish("copy", errno_to_result(e));
			return;
		}
		ssize_t off = 0;
		while (off < r) {
			ssize_t const w = ::write(fout, buf + off, (size_t)(r - off));
			if (w < 0) {
				int const e = errno;
				::close(fin);
				::close(fout);
				::unlink(d.constData());
				QString const msg = QStringLiteral("copy: write %1: %2")
					.arg(dst).arg(std::strerror(e));
				_status(msg);
				Genode::log("sponge_files: ", msg.toUtf8().constData());
				_publish("copy", errno_to_result(e));
				return;
			}
			off += w;
		}
		total += r;
	}
	::close(fin);
	::close(fout);

	QString const msg = QStringLiteral("copied %1 -> %2 (%3 bytes)")
		.arg(src).arg(dst).arg(total);
	_status(msg);
	Genode::log("sponge_files: ", msg.toUtf8().constData());
	_publish("copy", "ok");
}


void Files_window::on_delete_clicked()
{
	QListWidgetItem *const item = _list->currentItem();
	if (!item) {
		_status("delete: no selection");
		_publish("delete", "refused");
		return;
	}
	QString const name = item->text();
	QString const target = _resolve(name);

	if (!_is_writable(target)) {
		/*
		 * Refused in the read-only area. Surface it in three places
		 * (UI/log/report) so the probe can assert it without parsing
		 * pixels — the misleading_success_output adversarial class.
		 */
		QString const msg = QStringLiteral(
			"delete %1 refused: read-only area").arg(target);
		_status(msg);
		Genode::log("sponge_files: ", msg.toUtf8().constData());
		_publish("delete", "refused");
		return;
	}
	_delete(target);
}


void Files_window::_delete(QString path)
{
	QByteArray const p = path.toUtf8();
	if (::unlink(p.constData()) != 0) {
		int const e = errno;
		QString const msg = QStringLiteral("delete %1: %2")
			.arg(path).arg(std::strerror(e));
		_status(msg);
		Genode::log("sponge_files: ", msg.toUtf8().constData());
		_publish("delete", errno_to_result(e));
		return;
	}
	QString const msg = QStringLiteral("deleted %1").arg(path);
	_status(msg);
	Genode::log("sponge_files: ", msg.toUtf8().constData());
	_refresh();
	_publish("delete", "ok");
}


/* =================== Listing & reporting =================== */


QString Files_window::_resolve(QString const &relative) const
{
	if (relative.isEmpty()) return _cwd;
	if (relative.startsWith('/')) return relative;   /* already absolute */

	if (relative == "..") {
		if (_cwd == "/" || _cwd.isEmpty()) return "/";
		int const idx = _cwd.lastIndexOf('/');
		if (idx <= 0) return "/";
		return _cwd.left(idx);
	}

	if (_cwd.endsWith('/')) return _cwd + relative;
	return _cwd + "/" + relative;
}


void Files_window::_populate_list()
{
	_list->clear();

	QByteArray const p = _cwd.toUtf8();
	DIR *dir = ::opendir(p.constData());
	if (!dir) {
		int const e = errno;
		QString const msg = QStringLiteral("opendir %1: %2")
			.arg(_cwd).arg(std::strerror(e));
		_status(msg);
		Genode::log("sponge_files: ", msg.toUtf8().constData());
		return;
	}

	/* Add ".." entry for every directory except root. */
	if (_cwd != "/") {
		auto *up = new QListWidgetItem("..", _list);
		up->setData(Qt::UserRole, QString("dir"));
	}

	QStringList names;
	struct dirent *de = nullptr;
	while ((de = ::readdir(dir)) != nullptr) {
		QString const n = QString::fromUtf8(de->d_name);
		if (n == "." || n == "..") continue;
		names.append(n);
	}
	::closedir(dir);

	names.sort(Qt::CaseInsensitive);

	for (QString const &n : names) {
		QString const full = _resolve(n);
		QByteArray const f = full.toUtf8();
		struct stat st;
		bool is_dir = false;
		if (::stat(f.constData(), &st) == 0) {
			is_dir = S_ISDIR(st.st_mode);
		}

		auto *item = new QListWidgetItem(n, _list);
		item->setData(Qt::UserRole, is_dir ? QString("dir") : QString("file"));
		if (is_dir) item->setText(n + "/");
	}
}


void Files_window::_refresh()
{
	_populate_list();
}


void Files_window::_status(QString msg)
{
	_status_label->setText(msg);
}


void Files_window::_publish(QString last_action, QString last_result)
{
	/*
	 * Report schema (read by files_probe):
	 *   <files entries="N" path="/cwd">
	 *     <last_action name="navigate|open|copy|delete|up|init"
	 *                  result="ok|refused|no-such-file|..."/>
	 *   </files>
	 *
	 * The list count excludes the synthetic ".." entry so the probe can
	 * reason about the actual file count deterministically.
	 */
	QStringList names;
	for (int i = 0; i < _list->count(); ++i) {
		QString const t = _list->item(i)->text();
		if (t == "..") continue;
		names.append(t);
	}
	unsigned const entries = (unsigned)names.size();

	QByteArray const path = _cwd.toUtf8();
	QByteArray const action = last_action.toUtf8();
	QByteArray const result = last_result.toUtf8();

	/* Discard Attempt<Ok, Buffer_error> (nodiscard): probe polls, tolerates retry. */
	(void)_report.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("path", path.constData());
		g.attribute("entries", entries);
		g.node("last_action", [&] {
			g.attribute("name",   action.constData());
			g.attribute("result", result.constData());
		});
	});
}


/* =================== Request channel (automation default) =================== */


void Files_window::_poll_request()
{
	_request_rom.update();
	if (!_request_rom.valid()) return;

	QString op, a1, a2, sig, seq;

	try {
		Genode::Xml_node const req = _request_rom.xml();
		if (!req.has_type("request")) return;

		op  = QString::fromUtf8(
			req.attribute_value("op", Genode::String<32>()).string());
		a1  = QString::fromUtf8(
			req.attribute_value("arg1", Genode::String<256>()).string());
		a2  = QString::fromUtf8(
			req.attribute_value("arg2", Genode::String<256>()).string());
		seq = QString::fromUtf8(
			req.attribute_value("seq", Genode::String<16>()).string());
		sig = QStringLiteral("%1|%2|%3|%4").arg(seq, op, a1, a2);
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		return;
	}

	/* De-dup: ignore a ROM that has not changed since the last poll. */
	if (sig == _last_request_sig) return;
	_last_request_sig = sig;

	_dispatch_request(op, a1, a2);
}


void Files_window::_dispatch_request(QString const &op,
                                     QString const &arg1,
                                     QString const &arg2)
{
	if (op == "navigate") {
		_navigate(arg1);
	}
	else if (op == "up") {
		on_up_clicked();
	}
	else if (op == "open") {
		_open(arg1);
	}
	else if (op == "copy") {
		/* arg1 = src (any path), arg2 = dst (must be /writable/...). */
		if (!_is_writable(arg2)) {
			QString const msg = QStringLiteral(
				"copy refused: dst %1 not in /writable").arg(arg2);
			_status(msg);
			Genode::log("sponge_files: ", msg.toUtf8().constData());
			_publish("copy", "refused");
			return;
		}
		_copy(arg1, arg2);
	}
	else if (op == "delete") {
		if (!_is_writable(arg1)) {
			QString const msg = QStringLiteral(
				"delete refused: %1 not in /writable").arg(arg1);
			_status(msg);
			Genode::log("sponge_files: ", msg.toUtf8().constData());
			_publish("delete", "refused");
			return;
		}
		_delete(arg1);
	}
	else if (op == "noop") {
		/* Used by the probe to verify the channel works without altering
		 * state. Just republish. */
		_publish("noop", "ok");
	}
	else {
		_status("unknown op: " + op);
		_publish(op, "refused");
	}
}
