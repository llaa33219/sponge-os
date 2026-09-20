/* SPDX-License-Identifier: Apache-2.0
 *
 * ShortcutsTab implementation. See shortcuts_tab.h.
 */

#include "shortcuts_tab.h"

#include "../config/settings_controller.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace Sponge::Sponge_DE;


ShortcutsTab::ShortcutsTab(QWidget *parent)
:
	QWidget(parent)
{
	auto *root = new QVBoxLayout(this);

	auto *box = new QGroupBox(QStringLiteral("Key bindings"), this);
	auto *vb  = new QVBoxLayout(box);

	_table = new QTableWidget(0, 2, box);
	_table->setHorizontalHeaderLabels(
	    QStringList{ QStringLiteral("Action"), QStringLiteral("Key sequence") });
	_table->horizontalHeader()->setStretchLastSection(true);
	_table->verticalHeader()->setVisible(false);
	_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	_table->setSelectionMode(QAbstractItemView::SingleSelection);
	vb->addWidget(_table);

	{
		auto *row = new QHBoxLayout;
		_action_combo = new QComboBox(box);
		_action_combo->addItems({
		    QStringLiteral("launcher"),
		    QStringLiteral("focus_next"),
		    QStringLiteral("dismiss"),
		});
		_sequence_edit = new QLineEdit(box);
		_sequence_edit->setPlaceholderText(QStringLiteral("e.g. Super"));
		_add_button   = new QPushButton(QStringLiteral("Add"),    box);
		_remove_button = new QPushButton(QStringLiteral("Remove"), box);
		_apply_button  = new QPushButton(QStringLiteral("Apply"),  box);
		row->addWidget(new QLabel(QStringLiteral("Action:"), box));
		row->addWidget(_action_combo);
		row->addWidget(new QLabel(QStringLiteral("Key:"), box));
		row->addWidget(_sequence_edit);
		row->addWidget(_add_button);
		row->addWidget(_remove_button);
		row->addStretch();
		row->addWidget(_apply_button);
		vb->addLayout(row);
	}

	auto *hint = new QLabel(
	    QStringLiteral("Closed enum: launcher / focus_next / dismiss. "
	                   "Lines that fail validation reject the whole "
	                   "write; the editor does NOT save partial state."),
	    box);
	hint->setWordWrap(true);
	vb->addWidget(hint);

	root->addWidget(box);
	root->addStretch();

	connect(_add_button,    &QPushButton::clicked,
	        this, &ShortcutsTab::_on_add_row);
	connect(_remove_button, &QPushButton::clicked,
	        this, &ShortcutsTab::_on_remove_row);
	connect(_apply_button,  &QPushButton::clicked,
	        this, &ShortcutsTab::_on_apply);
}


void ShortcutsTab::set_controller(SettingsController *controller)
{
	_controller = controller;
}


void ShortcutsTab::_on_add_row()
{
	if (!_table) return;
	int const row = _table->rowCount();
	_table->insertRow(row);
	_table->setItem(row, 0, new QTableWidgetItem(_action_combo->currentText()));
	_table->setItem(row, 1, new QTableWidgetItem(_sequence_edit->text()));
}


void ShortcutsTab::_on_remove_row()
{
	if (!_table) return;
	int const row = _table->currentRow();
	if (row < 0) return;
	_table->removeRow(row);
}


void ShortcutsTab::_on_apply()
{
	if (!_controller || !_table) return;
	/*
	 * Serialize the table to "<action>\t<sequence>\n..." as the
	 * structured value. Empty table -> empty string (the
	 * validator rejects empty values per the W2 spec; the
	 * request_failed slot surfaces the structured error).
	 */
	QStringList lines;
	int const rows = _table->rowCount();
	for (int r = 0; r < rows; ++r) {
		QTableWidgetItem *action  = _table->item(r, 0);
		QTableWidgetItem *seq     = _table->item(r, 1);
		if (!action || !seq) continue;
		QString const a = action->text().trimmed();
		QString const s = seq->text().trimmed();
		if (a.isEmpty() || s.isEmpty()) continue;
		lines << (a + QLatin1Char('\t') + s);
	}
	QString const value = lines.join(QLatin1Char('\n'));
	_controller->request_set(QStringLiteral("shortcuts.bindings"), value);
}
