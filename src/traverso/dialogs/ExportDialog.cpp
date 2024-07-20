/**
    Copyright (C) 2005-2007 Remon Sijrier 
 
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

#include "ExportDialog.h"

#include <QFileDialog>
#include <QCloseEvent>

#include "TExportSpecification.h"
#include "Information.h"
#include "Project.h"
#include "ProjectManager.h"
#include "Sheet.h"

#include "widgets/ExportFormatOptionsWidget.h"


// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"



ExportDialog::ExportDialog( QWidget * parent )
	: QDialog(parent)
{
    setupUi(this);

    QBoxLayout* lay = qobject_cast<QBoxLayout*>(layout());
    if (lay) {
        m_formatOptionsWidget = new ExportFormatOptionsWidget(lay->widget());
        lay->insertWidget(1, m_formatOptionsWidget);
    }

    cancelExportButton->hide();
	QIcon icon = QApplication::style()->standardIcon(QStyle::SP_DirClosedIcon);
	fileSelectButton->setIcon(icon);
	
	set_project(pm().get_project());
	
	setMaximumSize(400, 450);
}


ExportDialog::~ ExportDialog( )
{
}


bool ExportDialog::is_safe_to_export()
{
	PENTER;
	if (m_project->is_recording()) {
		info().warning(tr("Export during recording is not supported!"));
		return false;
	}
	
	return true;
}


void ExportDialog::on_startButton_clicked( )
{
	if (!is_safe_to_export()) {
		return;
	}
	
	if (exportDirName->text().isEmpty()) {
        info().warning(tr("No Export Directory was given, please supply one first!"));
		return;
	}

    auto exportSpecification = m_project->get_export_specification();
	
	connect(m_project, SIGNAL(exportFinished()), this, SLOT(render_finished()));
    connect(exportSpecification, &TExportSpecification::exportMessage, this, [=](const QString& exportMessage) {
        exportMessagesLabel->setText(exportMessage);
    });
    connect(exportSpecification, &TExportSpecification::progressChanged, this, [=] (int progress) {
        progressBar->setValue(progress);
    });
    connect(cancelExportButton, &QPushButton::clicked, this, [=]() {
        exportSpecification->cancel_export();
    });

	// clear extraformats, it might be different now from previous runs!
    exportSpecification->extraFormat.clear();
	
    m_formatOptionsWidget->get_format_options(exportSpecification);
	
	if (allSheetsButton->isChecked()) {
        for (auto sheet : m_project->get_sheets()) {
            exportSpecification->add_sheet_to_export(sheet);
        }
	} else {
        exportSpecification->add_sheet_to_export(m_project->get_active_sheet());
    }

    QString exportDir = exportDirName->text();
    if (exportDir.size() > 1 && (exportDir.at(exportDir.size()-1).decomposition() != "/")) {
        exportDir.append("/");
	}

    exportSpecification->set_export_dir(exportDir);

    QString name = exportSpecification->get_export_dir();
    QFileInfo fi(exportSpecification->get_export_file_name());
	name += fi.completeBaseName() + ".toc";
    exportSpecification->tocFileName = name;

    m_project->export_project();
	
	startButton->hide();
	closeButton->hide();
    cancelExportButton->show();
}


void ExportDialog::on_closeButton_clicked()
{
	hide();
}


void ExportDialog::on_fileSelectButton_clicked( )
{
	if (!m_project) {
		info().information(tr("No project loaded, to export a project, load it first!"));
		return;
	}
	
    QString dirName = QFileDialog::getExistingDirectory(this, tr("Choose/create an export directory"), m_project->get_export_specification()->get_export_dir());
	
	if (!dirName.isEmpty()) {
		exportDirName->setText(dirName);
	}
}


void ExportDialog::render_finished( )
{
	disconnect(m_project, SIGNAL(exportFinished()), this, SLOT(render_finished()));
	disconnect(m_project, SIGNAL(exportStartedForSheet(Sheet*)), this, SLOT (set_exporting_sheet(Sheet*)));
	
	startButton->show();
	closeButton->show();
    cancelExportButton->hide();
	progressBar->setValue(0);
}

void ExportDialog::set_project(Project * project)
{
    if (! project)
    {
		info().information(tr("No project loaded, to export a project, load it first!"));
		setEnabled(false);

        return;
    }

    setEnabled(true);

    m_project = project;

    auto exportSpecification = m_project->get_export_specification();
    exportDirName->setText(exportSpecification->get_export_dir());
}



void ExportDialog::closeEvent(QCloseEvent * event)
{
	if (closeButton->isHidden()) {
		event->setAccepted(false);
		return;
	}
	QDialog::closeEvent(event);
}

void ExportDialog::reject()
{
	if (closeButton->isHidden()) {
		return;
	}
	hide();
}

