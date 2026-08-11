/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * TaskInfo — public POD struct for one tasklist entry.
 *
 * The struct is shared between the TasklistWidget (render target) and
 * the TasklistController (state owner). It lives in its own header so
 * neither module has to include the other's Q_OBJECT-annotated header.
 *
 * The struct is intentionally in the GLOBAL namespace so the
 * Qt-moc pass for the controller's Q_OBJECT doesn't double-namespace
 * the type (moc injects the class's namespace into all referenced
 * types, which would otherwise produce
 * `Sponge::Sponge_DE::Sponge::Sponge_DE::TaskInfo`).
 *
 * Fields:
 *   - label: the layouter's Window::Label (wm session label, e.g.
 *     "pkg_runtime -> pkg_gui_demo"). Stable identifier used by the
 *     controller for focus_request and rules updates.
 *   - title: the layouter's <window title="..."> attribute (concat
 *     of label + " " + Qt window title). User-visible label rendered
 *     on the entry.
 *   - x, y, w, h: tracked window geometry (from window_layout).
 *   - focused:    tasklist entry is the focused window.
 *   - minimized:  tasklist entry is parked off-screen (x=-32000, y=-32000).
 *   - has_alpha:  window has alpha (Qt apps). Renders a 2 px accent
 *     strip on the left edge of the entry.
 */

#pragma once

#include <QList>
#include <QString>

struct TaskInfo {
	QString  label;
	QString  title;
	int      x      { 0 };
	int      y      { 0 };
	unsigned w      { 0 };
	unsigned h      { 0 };
	bool     focused   { false };
	bool     minimized { false };
	bool     has_alpha { false };
};
