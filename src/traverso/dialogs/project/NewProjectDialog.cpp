/*
    Copyright (C) 2007-2008 Remon Sijrier 
 
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

#include "NewProjectDialog.h"

#include <QDir>
#include <QStringList>
#include <QMessageBox>
#include <QTextStream>
#include <QDomDocument>
#include <QFileDialog>
#include <QHeaderView>
#include <QToolButton>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QList>
#include <QFileInfo>
#include <QFile>
#include <QCheckBox>
#include <QRadioButton>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QIcon>

#include "TConfig.h"
#include "TExportSpecification.h"
#include "Information.h"
#include "ProjectManager.h"
#include "ResourcesManager.h"
#include <Project.h>
#include "ProjectManager.h"
#include <Sheet.h>
#include <AudioTrack.h>
#include <Utils.h>
#include <CommandGroup.h>
#include "TAudioFileImportCommand.h"
#include "AudioFileCopyConvert.h"
#include "ReadSource.h"

#include "widgets/ExportFormatOptionsWidget.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

NewProjectDialog::NewProjectDialog( QWidget * parent )
	: QDialog(parent)
{
	setupUi(this);
        trackCountSpinBox->setValue(config().get_property("Sheet", "trackCreationCount", 1).toInt());
	
	use_template_checkbox_state_changed(Qt::Unchecked);
	update_template_combobox();
        update_projects_directory_line_edit();

	buttonAdd->setIcon(QIcon(":/add"));
	buttonRemove->setIcon(QIcon(":/remove"));
	buttonUp->setIcon(QIcon(":/up"));
	buttonDown->setIcon(QIcon(":/down"));

	buttonRemove->setEnabled(false);
	buttonUp->setEnabled(false);
	buttonDown->setEnabled(false);

	buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);

	m_converter = new AudioFileCopyConvert();
	m_exportSpec = new TExportSpecification;
	m_buttonGroup = new QButtonGroup(this);
	m_buttonGroup->addButton(radioButtonImport, 0);
	m_buttonGroup->addButton(radioButtonEmpty, 1);

    connect(useTemplateCheckBox, SIGNAL(stateChanged(int)), this, SLOT(use_template_checkbox_state_changed(int)));
	connect(buttonAdd, SIGNAL(clicked()), this, SLOT(add_files()));
	connect(buttonRemove, SIGNAL(clicked()), this, SLOT(remove_files()));
	connect(buttonUp, SIGNAL(clicked()), this, SLOT(move_up()));
	connect(buttonDown, SIGNAL(clicked()), this, SLOT(move_down()));

    connect(m_converter, SIGNAL(taskFinished(QString,int,QString)), this, SLOT(load_file(QString,int,QString)));
    connect(m_buttonGroup, SIGNAL(idClicked(int)), stackedWidget, SLOT(setCurrentIndex(int)));

        connect(&pm(), SIGNAL(currentProjectDirChanged()), this, SLOT(update_projects_directory_line_edit()));
}

NewProjectDialog::~ NewProjectDialog( )
{}


void NewProjectDialog::accept( )
{

        // do we have the name of the project to create ?
	QString title = newProjectName->text();
	
	if (title.length() == 0) {
		info().information(tr("You must supply a name for the project!") );
		return;
	}


	// first test if project exists already
	if (pm().project_exists(title)) {
		switch (QMessageBox::information(this,
            tr("Traverso - Question"),
            tr("The Project \"%1\" already exists, do you want to remove it and replace it with a new one ?").arg(title),
            QMessageBox::StandardButton::Yes,
            QMessageBox::StandardButton::Cancel))
		{
            case QMessageBox::StandardButton::Yes:
				pm().remove_project(title);
				break;
			default:
				return;
				break;
		}
	}

	Project* project;
	
	int numSheets = sheetCountSpinBox->value();
	int numTracks = trackCountSpinBox->value();
	
	int index = templateComboBox->currentIndex();
	bool usetemplate = false;

	if (useTemplateCheckBox->isChecked() && index >= 0) {
		usetemplate = true;
	}
	
	// check which method to use. If there are items in the treeWidgetFiles, ignore
	// settings in the "empty project" tab. Else use settings from "empty project" tab.
	int items = treeWidgetFiles->topLevelItemCount();
	bool loadFiles = false;
        if (items > 0) {
		//there are items in the treeWidgetFiles
		loadFiles = true;
		numSheets = 1;
		numTracks = items;
                project = pm().create_new_project(numSheets, numTracks, title);
	} else {
		//no items in the treeWidgetFiles
		if (usetemplate) {
			project = pm().create_new_project(QDir::homePath() + "/.traverso/ProjectTemplates/" + 
					templateComboBox->itemText(index) + ".tpt", title);
                        if (! project) {
                                info().warning(tr("Couldn't create project (%1)").arg(title) );
                                return;
                        }
		} else {
			project = pm().create_new_project(numSheets, numTracks, title);
                        if (! project) {
                                info().warning(tr("Couldn't create project (%1)").arg(title) );
                                return;
                        }
		}
	}

        // template loads do it all by itself, but for non-template project creation
        // we have to save the project and load if afterwards.. maybe it should be handled
        // by ProjectManager?
        if (!usetemplate) {
                project->set_description(descriptionTextEdit->toPlainText());
                project->set_engineer(newProjectEngineer->text());
                project->save();
        }

	
	delete project;
	
	pm().load_project(title);

	if (loadFiles) {
		if (checkBoxCopy->isChecked()) {
			copy_files();
		} else {
			load_all_files();
		}
	}

	hide();
}

void NewProjectDialog::use_template_checkbox_state_changed(int state)
{
	if (state == Qt::Checked) {
		templateComboBox->setEnabled(true);
		trackCountSpinBox->setEnabled(false);
	} else {
		templateComboBox->setEnabled(false);
		trackCountSpinBox->setEnabled(true);
	}
}

void NewProjectDialog::update_template_combobox()
{
	QDir templatedir(QDir::homePath() + "/.traverso/ProjectTemplates");
	
	foreach (QString filename, templatedir.entryList(QDir::Files | QDir::NoDotAndDotDot)) {
		templateComboBox->insertItem(0, filename.remove(".tpt"));
	}
}

void NewProjectDialog::add_files()
{
        QString importdir = config().get_property("Project", "importdir", "").toString();
        if (importdir.isEmpty() || importdir.isNull()) {
                importdir = QDir::homePath();
        }

	QStringList list = QFileDialog::getOpenFileNames(this, tr("Open Audio Files"),
                        importdir,
                        tr("Audio files (*.wav *.flac *.ogg *.mp3 *.wv *.w64)"));
        
        if (list.size()) {
                QFileInfo info(list.at(0));
                QString importdir = info.absoluteDir().path();
                config().set_property("Project", "importdir", importdir);
        }

	for(int i = 0; i < list.size(); ++i)
	{
		QStringList labels;
		QFileInfo finfo(list.at(i));
        labels << finfo.completeBaseName() << finfo.fileName();

		QTreeWidgetItem* item = new QTreeWidgetItem(treeWidgetFiles, labels, 0);
		item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled);
		item->setData(0, Qt::ToolTipRole, finfo.absoluteFilePath());
		treeWidgetFiles->addTopLevelItem(item);
	}

	if (treeWidgetFiles->topLevelItemCount()) {
		buttonRemove->setEnabled(true);
		buttonUp->setEnabled(true);
		buttonDown->setEnabled(true);
	}
}

void NewProjectDialog::remove_files()
{
	QList<QTreeWidgetItem*> selection = treeWidgetFiles->selectedItems();

	if (selection.isEmpty())
	{
		return;
	}

	while (!selection.isEmpty())
	{
		QTreeWidgetItem *it = selection.takeLast();
		delete it;
	}

	if (!treeWidgetFiles->topLevelItemCount()) {
		buttonRemove->setEnabled(false);
		buttonUp->setEnabled(false);
		buttonDown->setEnabled(false);
	}
}

void NewProjectDialog::copy_files()
{
    PENTER;
        emit numberOfFiles(treeWidgetFiles->topLevelItemCount());

	QList<QFileInfo> list;
	QStringList trackNameList;
	while(treeWidgetFiles->topLevelItemCount()) {
		QTreeWidgetItem* item = treeWidgetFiles->takeTopLevelItem(0);
		list.append(QFileInfo(item->data(0, Qt::ToolTipRole).toString()));
		trackNameList.append(item->text(0));
		delete item;
	}

	QString destination = pm().get_project()->get_root_dir() + "/audiosources/";
	
	// copy to project dir
	for (int n = 0; n < list.size(); ++n)
	{
		QString fn = destination + list.at(n).fileName();

		// TODO: check for free disk space
		
		// TODO: offer file format conversion while copying: format options widget not there yet.
//		m_formatOptionsWidget->get_format_options(m_exportSpec);

		ReadSource* readsource = resources_manager()->import_source(list.at(n).absolutePath() + "/", list.at(n).fileName());

		if (readsource) {
			m_converter->enqueue_task(readsource, m_exportSpec, destination, list.at(n).fileName(), n, trackNameList.at(n));
		}
	}
}

void NewProjectDialog::load_all_files()
{
    PENTER;
	int i = 0;

	while(treeWidgetFiles->topLevelItemCount()) {
		QTreeWidgetItem* item = treeWidgetFiles->takeTopLevelItem(0);
		QString f = item->data(0, Qt::ToolTipRole).toString();
		QString n = item->text(0);
        qDebug() << n;
		delete item;

		load_file(f, i, n);
		++i;
	}
}

void NewProjectDialog::load_file(const QString &fileName, int i, QString trackname)
{
        Sheet* sheet = qobject_cast<Sheet*>(pm().get_project()->get_current_session());

	if (!sheet) {
		return;
	}

        QList<AudioTrack*> tracks = sheet->get_audio_tracks();

        if (i >= tracks.size()) {
                return;
        }

        AudioTrack* track = tracks.at(i);

        if (!track) {
                return;
        }

    TAudioFileImportCommand* import = new TAudioFileImportCommand(track);
    import->set_file_name(fileName);
    printf("renaming track to %s\n", trackname.toLatin1().data());
    track->set_name(trackname);
	import->set_track(track);
    import->set_import_location(TTimeRef());
	if (import->create_readsource() != -1) {
		TCommand::process_command(import);
	}
}

void NewProjectDialog::move_up()
{
	QList<QTreeWidgetItem*> selection = treeWidgetFiles->selectedItems();

	if (selection.isEmpty())
	{
		return;
	}

    std::sort(selection.begin(), selection.end(), [& selection](QTreeWidgetItem* left, QTreeWidgetItem* right) {
        return selection.indexOf(left) < selection.indexOf(right);
    });

	int firstIndex = treeWidgetFiles->topLevelItemCount();
	QList<int> indexList;

	foreach(QTreeWidgetItem *it, selection) {
	    int idx = treeWidgetFiles->indexOfTopLevelItem(it);
	    firstIndex = qMin(idx, firstIndex);
	}

	firstIndex = firstIndex > 0 ? firstIndex - 1 : firstIndex;

	QList<QTreeWidgetItem*> tempList;
	while (selection.size())
	{
		QTreeWidgetItem *it = treeWidgetFiles->takeTopLevelItem(treeWidgetFiles->indexOfTopLevelItem(selection.takeFirst()));
		treeWidgetFiles->insertTopLevelItem(firstIndex, it);
		it->setSelected(true);
		++firstIndex;
	}
}

void NewProjectDialog::move_down()
{
	QList<QTreeWidgetItem*> selection = treeWidgetFiles->selectedItems();

	if (selection.isEmpty())
	{
		return;
	}

    std::sort(selection.begin(), selection.end(), [& selection](QTreeWidgetItem* left, QTreeWidgetItem* right) {
        return selection.indexOf(left) < selection.indexOf(right);
    });

    int firstIndex = 0;
	QList<int> indexList;

	foreach(QTreeWidgetItem *it, selection) {
	    int idx = treeWidgetFiles->indexOfTopLevelItem(it);
	    firstIndex = qMax(idx, firstIndex);
	}

	firstIndex = firstIndex < treeWidgetFiles->topLevelItemCount() - 1 ? firstIndex + 1 : firstIndex;

	while (selection.size()) {
		int idx = treeWidgetFiles->indexOfTopLevelItem(selection.takeFirst());
		QTreeWidgetItem *it = treeWidgetFiles->takeTopLevelItem(idx);
		treeWidgetFiles->insertTopLevelItem(firstIndex, it);
		it->setSelected(true);
	}
}

AudioFileCopyConvert* NewProjectDialog::get_converter()
{
	return m_converter;
}

void NewProjectDialog::on_changeProjectsDirButton_clicked()
{
        QString path = pm().get_projects_directory();
        QString newPath = QFileDialog::getExistingDirectory(this,
                        tr("Choose an existing or create a new Project Directory"), path);

        if (newPath.isEmpty() || newPath.isNull()) {
                return;
        }

        QDir dir;

        QFileInfo fi(newPath);
        if (dir.exists(newPath) && !fi.isWritable()) {
                QMessageBox::warning( 0, tr("Traverso - Warning"),
                                      tr("This directory is not writable by you! \n") +
                                        tr("Please check permission for this directory or "
                                        "choose another one:\n\n %1").arg(newPath) );
                return;
        }


        if (dir.exists(newPath)) {
// 		QMessageBox::information( interface, tr("Traverso - Information"), tr("Using existing Project directory: %1\n").arg(newPath), "OK", 0 );
        } else if (!dir.mkpath(newPath)) {
                QMessageBox::warning( this, tr("Traverso - Warning"), tr("Unable to create Project directory! \n") +
                                tr("Please check permission for this directory: %1").arg(newPath) );
                return;
        } else {
                QMessageBox::information( this,
                    tr("Traverso - Information"),
                    tr("Created new Project directory for you here: %1\n").arg(newPath),
                    QMessageBox::StandardButton::Ok );
        }

        pm().set_current_project_dir(newPath);

        update_projects_directory_line_edit();
}

void NewProjectDialog::update_projects_directory_line_edit()
{
        QString path = pm().get_projects_directory();
        projectsDirLineEdit->setText(path);
}

//eof
