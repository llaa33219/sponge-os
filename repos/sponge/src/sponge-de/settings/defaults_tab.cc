/* SPDX-License-Identifier: Apache-2.0
 *
 * DefaultsTab implementation. See defaults_tab.h.
 */

#include "defaults_tab.h"

#include <base/log.h>

#include "../config/settings_controller.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Sponge::Sponge_DE;


DefaultsTab::DefaultsTab(QWidget *parent)
:
	QWidget(parent)
{
	auto *root = new QVBoxLayout(this);

	auto *box = new QGroupBox(QStringLiteral("Baked defaults"), this);
	auto *vb  = new QVBoxLayout(box);

	_description = new QLabel(
	    QStringLiteral("Resets the panel height, visible widgets, clock "
	                   "format, launcher sort order, and active theme to "
	                   "the values baked into the release media.\n\n"
	                   "Other user-set keys (panel position, background "
	                   "color and image, leitzentrale state, keyboard "
	                   "shortcuts) are preserved."),
	    box);
	_description->setWordWrap(true);
	vb->addWidget(_description);

	_reset = new QPushButton(QStringLiteral("Reset to baked defaults"), box);
	vb->addWidget(_reset);

	root->addWidget(box);
	root->addStretch();

	connect(_reset, &QPushButton::clicked,
	        this, &DefaultsTab::_on_reset);
}


void DefaultsTab::set_controller(SettingsController *controller)
{
	_controller = controller;
}


void DefaultsTab::_on_reset()
{
	if (!_controller) return;
	/*
	 * Confirm dialog: the reset is reversible on next boot's
	 * user-write, but the immediate user-set values for the
	 * five baked keys are lost. A confirmation modal makes the
	 * user acknowledge the action (AGENTS.md §1.1 explicit
	 * affordance for destructive controls).
	 */
	auto const reply = QMessageBox::question(
	    this,
	    QStringLiteral("Reset to baked defaults"),
	    QStringLiteral("Reset the panel / theme / launcher / clock keys "
	                   "to the baked default profile?\n\n"
	                   "Other keys are preserved."),
	    QMessageBox::Yes | QMessageBox::Cancel);
	if (reply != QMessageBox::Yes) return;

	Genode::log("settings: defaults reset requested");
	/*
	 * The bake.applied=no write triggers configd's reset path
	 * (sponge_configd/main.cc W3 sentinel handling). configd
	 * reapplies ONLY the baked keys + theme.active, leaves
	 * every other user-set key untouched, then sets
	 * bake.applied=yes and regenerates the broadcast.
	 */
	_controller->request_set(QStringLiteral("bake.applied"),
	                         QStringLiteral("no"));
}
