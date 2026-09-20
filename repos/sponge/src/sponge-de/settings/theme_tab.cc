/* SPDX-License-Identifier: Apache-2.0
 *
 * ThemeTab implementation. See theme_tab.h for the API contract.
 */

#include "theme_tab.h"

#include "../config/settings_controller.h"

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Sponge::Sponge_DE;


ThemeTab::ThemeTab(QWidget *parent)
:
	QWidget(parent)
{
	auto *root = new QVBoxLayout(this);

	auto *box = new QGroupBox(QStringLiteral("Active theme"), this);
	auto *vb  = new QVBoxLayout(box);

	_combo = new QComboBox(box);
	/*
	 * The four W3 shipped themes. The Phase 11 / Phase 14 themes
	 * live under repos/sponge/src/sponge-de/themes/*.theme.
	 * Adding user-staged themes is Phase 17+ scope (per the
	 * non-goals list in docs/plans/phase16-daily-desktop-
	 * defaults.md).
	 */
	_combo->addItems({
	    QStringLiteral("default"),
	    QStringLiteral("light"),
	    QStringLiteral("dark"),
	    QStringLiteral("compact"),
	});

	vb->addWidget(_combo);

	auto *hint = new QLabel(
	    QStringLiteral("Selecting a theme writes theme.active. "
	                   "Live reload picks the new color palette without restart."),
	    box);
	hint->setWordWrap(true);
	vb->addWidget(hint);

	_apply = new QPushButton(QStringLiteral("Apply"), box);
	vb->addWidget(_apply);

	root->addWidget(box);
	root->addStretch();

	connect(_apply, &QPushButton::clicked,
	        this, &ThemeTab::_on_apply);
}


void ThemeTab::set_controller(SettingsController *controller)
{
	_controller = controller;
}


void ThemeTab::_on_apply()
{
	if (!_controller) return;
	_controller->request_set(QStringLiteral("theme.active"),
	                         _combo->currentText());
}
