/* SPDX-License-Identifier: Apache-2.0
 *
 * BackgroundController — see background_controller.h.
 */

#include "background_controller.h"

#include <base/log.h>

using namespace Sponge::Sponge_DE;


BackgroundController::BackgroundController(QObject *parent)
:
	QObject(parent)
{
}


void BackgroundController::applyBackgroundColor(QString color)
{
	if (color.isEmpty() || color == _last_color) return;
	_last_color = color;
	Genode::log("sponge-de: background.color=", color.toUtf8().constData());
	emit background_color_changed(color);
}


void BackgroundController::applyBackgroundImage(QString path)
{
	if (path.isEmpty() || path == _last_image) return;
	_last_image = path;
	Genode::log("sponge-de: background.image=", path.toUtf8().constData());
	emit background_image_changed(path);
}