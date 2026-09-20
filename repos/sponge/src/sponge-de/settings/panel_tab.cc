/* SPDX-License-Identifier: Apache-2.0
 *
 * PanelTab implementation. See panel_tab.h for the API contract.
 */

#include "panel_tab.h"

#include "../config/settings_controller.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace Sponge::Sponge_DE;


PanelTab::PanelTab(QWidget *parent)
:
	QWidget(parent)
{
	auto *root = new QVBoxLayout(this);

	/* height */
	{
		auto *box = new QGroupBox(QStringLiteral("Panel height"), this);
		auto *vb  = new QVBoxLayout(box);
		_height_spin = new QSpinBox(box);
		_height_spin->setRange(16, 128);
		_height_spin->setValue(28);
		_height_spin->setSingleStep(4);
		vb->addWidget(_height_spin);
		auto *hint = new QLabel(
		    QStringLiteral("Range: 16..128 px (the W2 panel.height validator)."),
		    box);
		hint->setWordWrap(true);
		vb->addWidget(hint);
		root->addWidget(box);
	}

	/* position */
	{
		auto *box = new QGroupBox(QStringLiteral("Panel position"), this);
		auto *vb  = new QVBoxLayout(box);
		auto *row = new QHBoxLayout;
		_position_grp = new QButtonGroup(box);
		_position_grp->setExclusive(true);
		struct P { QString label; bool enabled; QString tip; };
		QVector<P> const opts = {
			{ QStringLiteral("Top"),    true,  QString() },
			{ QStringLiteral("Bottom"), true,  QString() },
			{ QStringLiteral("Left"),   false, QStringLiteral(
			      "Left position is deferred to Phase 17+ (U16.2 / D16.2 "
			      "honest-disclosure rule). The enum token IS accepted by "
			      "the configd validator; the live panel menu enables it.") },
			{ QStringLiteral("Right"),  false, QStringLiteral(
			      "Right position is deferred to Phase 17+ (U16.2 / D16.2).") },
		};
		for (int i = 0; i < opts.size(); ++i) {
			auto *rb = new QRadioButton(opts[i].label, box);
			_position_grp->addButton(rb, i);
			if (!opts[i].enabled) {
				rb->setEnabled(false);
				if (!opts[i].tip.isEmpty())
					rb->setToolTip(opts[i].tip);
			}
			row->addWidget(rb);
		}
		/* Default: bottom */
		{
			QAbstractButton *bb = _position_grp->button(1);
			if (bb) bb->setChecked(true);
		}
		vb->addLayout(row);
		root->addWidget(box);
	}

	/* visible widgets */
	{
		auto *box = new QGroupBox(QStringLiteral("Visible widgets"), this);
		auto *vb  = new QVBoxLayout(box);
		_clock_chk    = new QCheckBox(QStringLiteral("Clock"),    box);
		_launcher_chk = new QCheckBox(QStringLiteral("Launcher"), box);
		_tasklist_chk = new QCheckBox(QStringLiteral("Tasklist"), box);
		/* Default: clock,launcher (matches the W2 default). */
		_clock_chk->setChecked(true);
		_launcher_chk->setChecked(true);
		vb->addWidget(_clock_chk);
		vb->addWidget(_launcher_chk);
		vb->addWidget(_tasklist_chk);
		_apply_widgets = new QPushButton(QStringLiteral("Apply visible widgets"),
		                                 box);
		vb->addWidget(_apply_widgets);
		root->addWidget(box);
	}

	root->addStretch();

	/* Connections. The height spinbox writes on valueChanged; the
	 * position group writes on button(id) toggled. The Apply button
	 * below the checkboxes writes the comma-list. */
	connect(_height_spin,
	        static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
	        this, &PanelTab::_on_apply_height);
	connect(_position_grp,
	        static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::idClicked),
	        this, &PanelTab::_on_apply_position);
	connect(_apply_widgets, &QPushButton::clicked,
	        this, &PanelTab::_on_apply_visible_widgets);
}


void PanelTab::set_controller(SettingsController *controller)
{
	_controller = controller;
}


void PanelTab::_on_apply_height(int v)
{
	if (!_controller) return;
	_controller->request_set(QStringLiteral("panel.height"),
	                         QString::number(v));
}


void PanelTab::_on_apply_position(int id)
{
	if (!_controller) return;
	QString value;
	switch (id) {
	case 0: value = QStringLiteral("top");    break;
	case 1: value = QStringLiteral("bottom"); break;
	case 2: value = QStringLiteral("left");   break;
	case 3: value = QStringLiteral("right");  break;
	default: return; /* unknown id; refuse silently (no F2 yet) */
	}
	_controller->request_set(QStringLiteral("panel.position"), value);
}


void PanelTab::_on_apply_visible_widgets()
{
	if (!_controller) return;
	QStringList tokens;
	if (_clock_chk    && _clock_chk->isChecked())    tokens << QStringLiteral("clock");
	if (_launcher_chk && _launcher_chk->isChecked()) tokens << QStringLiteral("launcher");
	if (_tasklist_chk && _tasklist_chk->isChecked()) tokens << QStringLiteral("tasklist");
	_controller->request_set(QStringLiteral("panel.visible_widgets"),
	                         tokens.join(QLatin1Char(',')));
}
