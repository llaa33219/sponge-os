/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Qt bridge helpers for the Sponge DE theme model.
 *
 * theme_loader.h is intentionally Qt-free (it must stay usable by
 * non-Qt components and unit tests). This header is the single place
 * where Theme values are converted into Qt types, so widgets never
 * re-implement color conversion and no color value is ever hardcoded
 * in widget code (AGENTS.md §3.4).
 */

#pragma once

#include <QColor>
#include <QString>

#include "theme_loader.h"

namespace Sponge::Sponge_DE::Theme {

/* Convert a theme Color (0xRRGGBB) into a QColor. */
inline QColor to_qcolor(Color color)
{
	return QColor(color.r(), color.g(), color.b());
}

/* Convert a theme Color into a "#RRGGBB" string for Qt stylesheets. */
inline QString to_css(Color color)
{
	return QString::asprintf("#%06x", color.raw);
}

} /* namespace Sponge::Sponge_DE::Theme */
