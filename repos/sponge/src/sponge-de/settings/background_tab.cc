/* SPDX-License-Identifier: Apache-2.0
 *
 * BackgroundTab implementation. See background_tab.h.
 */

#include "background_tab.h"

#include <base/log.h>

#include "../config/settings_controller.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Sponge::Sponge_DE;


BackgroundTab::BackgroundTab(QWidget *parent)
:
	QWidget(parent)
{
	auto *root = new QVBoxLayout(this);

	/* Color picker row */
	{
		auto *box = new QGroupBox(QStringLiteral("Solid color"), this);
		auto *vb  = new QVBoxLayout(box);
		auto *row = new QHBoxLayout;
		_color_edit = new QLineEdit(QStringLiteral("#1e1e2e"), box);
		_color_edit->setInputMask(QStringLiteral("\\#HHHHHH;"));
		_color_edit->setMaximumWidth(120);
		_color_apply = new QPushButton(QStringLiteral("Apply"), box);
		row->addWidget(new QLabel(QStringLiteral("#"), box));
		row->addWidget(_color_edit);
		row->addWidget(_color_apply);
		row->addStretch();
		vb->addLayout(row);
		auto *hint = new QLabel(
		    QStringLiteral("Hex `#RRGGBB` (7 chars). The W2 configd "
		                   "validator rejects any other shape with a "
		                   "structured error."),
		    box);
		hint->setWordWrap(true);
		vb->addWidget(hint);
		root->addWidget(box);
	}

	/* Image row */
	{
		auto *box = new QGroupBox(QStringLiteral("Background image"), this);
		auto *vb  = new QVBoxLayout(box);
		auto *row = new QHBoxLayout;
		_image_combo = new QComboBox(box);
		_image_apply = new QPushButton(QStringLiteral("Apply"), box);
		row->addWidget(_image_combo);
		row->addWidget(_image_apply);
		row->addStretch();
		vb->addLayout(row);
		auto *hint = new QLabel(
		    QStringLiteral("Single shipped image: `/system/background/"
		                   "default.png`. The W6 background widget "
		                   "paints the selected image; if blank, "
		                   "the solid color above is used."),
		    box);
		hint->setWordWrap(true);
		vb->addWidget(hint);
		root->addWidget(box);
	}

	root->addStretch();

	connect(_color_apply, &QPushButton::clicked,
	        this, &BackgroundTab::_on_apply_color);
	connect(_image_apply, &QPushButton::clicked,
	        this, &BackgroundTab::_on_apply_image);
}


void BackgroundTab::set_controller(SettingsController *controller)
{
	_controller = controller;
}


void BackgroundTab::set_image_allowlist(QStringList const &paths)
{
	if (!_image_combo) return;
	_image_combo->clear();
	if (paths.isEmpty()) {
		/* The W2 default; the configd validator's compiled-in
		 * allowlist also has this as the fallback. */
		_image_combo->addItem(QStringLiteral("/system/background/default.png"));
	} else {
		_image_combo->addItems(paths);
	}
}


void BackgroundTab::_on_apply_color()
{
	if (!_controller) return;
	QString const color = _color_edit->text();
	if (color.length() != 7 || !color.startsWith(QLatin1Char('#'))) {
		/*
		 * Defensive client-side check (Phase 16 F2 — no silent
		 * drops); the configd validator is the authoritative
		 * gate. We forward the rejected shape so the validator's
		 * structured error reply drives the QMessageBox.
		 */
		Genode::warning("sponge-de: settings: invalid color shape '",
		                color.toUtf8().constData(),
		                "' (expected #RRGGBB)");
		_controller->request_set(QStringLiteral("background.color"),
		                         color);
		return;
	}
	_controller->request_set(QStringLiteral("background.color"), color);
}


void BackgroundTab::_on_apply_image()
{
	if (!_controller) return;
	_controller->request_set(QStringLiteral("background.image"),
	                         _image_combo->currentText());
}
