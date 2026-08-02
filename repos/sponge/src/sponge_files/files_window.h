/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Files_window — main widget for sponge_files (Phase 7 todo 15).
 *
 * A minimal Qt6 Widgets file manager. The component runs under the
 * libc/Qt component model (Pattern B + qpa_init) and accesses its file
 * system purely through the libc POSIX API — the `<vfs>` node in the
 * metadata <config> mounts the read-only fixture area and the writable
 * RAM area under the component's root. No Genode Vfs library use, no
 * direct File_system session — minimum privilege (AGENTS.md §1.2).
 *
 * FEATURES (Alpha scope — todo 15):
 *   - Directory listing with double-click navigation.
 *   - "Up" button to navigate to the parent directory.
 *   - Open: clicking a file shows its first lines in the preview pane.
 *   - Copy: copy a selected file to /writable (writable area only).
 *   - Delete: delete a selected file in /writable (refused in /demo,
 *     which is mounted read-only).
 *
 * TESTABILITY:
 *   - A `files` Report publishes the current directory path, entry
 *     count, and the last action + result so a probe can verify every
 *     navigation/operation WITHOUT parsing pixels.
 *   - A `files_request` ROM carries the same operations the GUI emits
 *     (navigate/copy/delete/open), so the probe can drive copy/delete
 *     without relying on pixel-precise button clicks. The GUI is the
 *     manual escape hatch (AGENTS.md §3.3 rule 2); the request channel
 *     is the automation default (AGENTS.md §1.1: automation = default).
 *
 * THEME:
 *   The component loads the same INI theme format as sponge-de (see
 *   theme/theme_loader.h) once at startup. The `<theme source="..."/>`
 *   config node selects between the live sponge_themed ROM ("themed")
 *   and the bundled default.theme fallback ("default"). Both paths
 *   reuse sponge-de's semantics.
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <os/reporter.h>

#include <QObject>
#include <QString>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTextEdit;
class QTimer;

namespace Sponge::Sponge_Files {

namespace Theme { struct Theme; }

class Files_window : public QWidget
{
	Q_OBJECT

	public:

		Files_window(Genode::Env &env, Theme::Theme const &theme,
		             QWidget *parent = nullptr);

		void restyle(Theme::Theme const &theme);

	private slots:

		/* GUI handlers. */
		void on_item_double_clicked(QListWidgetItem *item);
		void on_up_clicked();
		void on_copy_clicked();
		void on_delete_clicked();

		/* Polls the files_request ROM (push not reliable under Qt's
		 * event loop — same lesson as sponge-de's ThemeController). */
		void _poll_request();

	private:

		Genode::Env &_env;

		QString _cwd { "/" };

		/* GUI elements. */
		QLabel      *_path_label   { nullptr };
		QListWidget *_list         { nullptr };
		QTextEdit   *_preview      { nullptr };
		QPushButton *_up_btn       { nullptr };
		QPushButton *_copy_btn     { nullptr };
		QPushButton *_delete_btn   { nullptr };
		QLabel      *_status_label { nullptr };
		QTimer      *_poll_timer   { nullptr };

		/* Testability channels. */
		Genode::Reporter _report            { _env, "files" };
		Genode::Attached_rom_dataspace _request_rom { _env, "files_request" };

		/* De-dup so an unchanged ROM does not reprocess. */
		QString _last_request_sig;

		/* Layout helpers. */
		void _apply_style(Theme::Theme const &theme);

		/* Core operations. Each emits a `files` Report with the result. */
		void _navigate(QString target);
		void _open(QString path);
		void _copy(QString src, QString dst);
		void _delete(QString path);

		/* Rebuild the list widget from _cwd, then publish the report. */
		void _refresh();

		void _publish(QString last_action, QString last_result);
		void _status(QString msg);

		/* Resolve absolute path. _resolve("/demo/x") = "/demo/x";
		 * _resolve("x") = _cwd + "/x"; _resolve("..") = parent. */
		QString _resolve(QString const &relative) const;

		/* Read the directory at _cwd and populate _list. */
		void _populate_list();

		/* True if `path` is inside /writable (the writable area). */
		bool _is_writable(QString const &path) const;

		/* Dispatch a single request op parsed from files_request. */
		void _dispatch_request(QString const &op, QString const &arg1,
		                       QString const &arg2);
};

} /* namespace Sponge::Sponge_Files */
