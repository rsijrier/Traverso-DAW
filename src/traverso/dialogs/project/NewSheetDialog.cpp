/*
Copyright (C) 2007 Remon Sijrier 

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/


#include "NewSheetDialog.h"

#include "TInformUser.h"
#include "TProject.h"
#include "TSheet.h"
#include "TConfig.h"
#include "TProjectManager.h"

#include <CommandGroup.h>
#include <QPushButton>

NewSheetDialog::NewSheetDialog(QWidget * parent)
	: QDialog(parent)
{
	setupUi(this);
	
        trackCountSpinBox->setValue(config().get_property("Sheet", "trackCreationCount", 1).toInt());
	
	set_project(pm().get_project());
	
	use_template_checkbox_state_changed(Qt::Unchecked);
	
	buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
	
    connect(&pm(), &TProjectManager::projectLoaded, this, &NewSheetDialog::set_project);
	connect(useTemplateCheckBox, &QCheckBox::checkStateChanged, this, &NewSheetDialog::use_template_checkbox_state_changed);
}

void NewSheetDialog::accept()
{
	if (! m_project) {
		tInformUser().information(tr("I can't create a new Sheet if there is no Project loaded!!"));
		return;
	}
	
	int count = countSpinBox->value();
	int trackcount = trackCountSpinBox->value();
	QString title = titleLineEdit->text();
	
	if (title.isEmpty()) {
		title = "Untitled";
	}
	
	int index = templateComboBox->currentIndex();
	bool usetemplate = false;
	QDomNode node;
	if (useTemplateCheckBox->isChecked() && index >= 0) {
		usetemplate = true;
		TSheet* templatesheet = m_project->get_sheet(templateComboBox->itemData(index).toLongLong());
		Q_ASSERT(templatesheet);
		QDomDocument doc("Sheet");
		node = templatesheet->get_state(doc, usetemplate);
	}
	
	CommandGroup* group = new CommandGroup(m_project, "");
	
	TSheet* firstNewSheet = nullptr;
	
	for (int i=0; i<count; ++i) {
		TSheet* sheet;
		if (usetemplate) {
			sheet = m_project->objects().make<TSheet>(m_project);
			sheet->set_state(node);
		} else {
			sheet = m_project->objects().make<TSheet>(m_project, trackcount);
		}
                sheet->set_name(title);
		group->add_command(m_project->add_sheet(sheet));
		if (i == 0) {
			firstNewSheet = sheet;
		}
	}
	
	group->setText(tr("Added %n Sheet(s)", "", count));
	TCommand::process_command(group);
	
	if (firstNewSheet) {
		m_project->set_current_session(firstNewSheet->get_id());
	}
	
	hide();
}

void NewSheetDialog::set_project(TProject * project)
{
	m_project = project;
	
	if (! m_project) {
		templateComboBox->clear();
		return;
	}
	
    connect(m_project, &TProject::sheetAdded, this, &NewSheetDialog::update_template_combo);
    connect(m_project, &TProject::sheetRemoved, this, &NewSheetDialog::update_template_combo);
	
	update_template_combo();
}

void NewSheetDialog::reject()
{
	hide();
}

void NewSheetDialog::update_template_combo()
{
	templateComboBox->clear();
	
	foreach(TSheet* sheet, m_project->get_sheets()) {
		QString text = "Sheet " + QString::number(m_project->get_sheet_index(sheet->get_id())) +
                                " " + sheet->get_name();
		
		templateComboBox->addItem(text, sheet->get_id());

                // FIXME ? Maybe the dialog should be modal, and be recreated each time it is shown
                // to avoid code complexity like this?
//		connect(sheet, &TSheet::propertyChanged, this, &NewSheetDialog::update_template_combo);
	}
}

void NewSheetDialog::use_template_checkbox_state_changed(int state)
{
	if (state == Qt::Checked) {
		templateComboBox->setEnabled(true);
		trackCountSpinBox->setEnabled(false);
	} else {
		templateComboBox->setEnabled(false);
		trackCountSpinBox->setEnabled(true);
	}
}

