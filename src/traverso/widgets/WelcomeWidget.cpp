/*
Copyright (C) 2010 Remon Sijrier

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


#include "WelcomeWidget.h"

#include "TConfig.h"
#include "ProjectManager.h"
#include "Project.h"
#include "TMainWindow.h"

#include <QMessageBox>
#include <QDir>
#include <QFileDialog>
#include <QKeyEvent>


WelcomeWidget::WelcomeWidget(QWidget *parent)
        : QWidget(parent)
{
        setupUi(this);
        welcomeTextBrowser->setOpenExternalLinks(true);

        update_previous_project_line_edit();
        update_projects_directory_line_edit();
        update_projects_combo_box();

        connect(loadPreviousProjectButton, SIGNAL(clicked()), this, SLOT(load_previous_project_button_clicked()));
        connect(loadExistingProjectButton, SIGNAL(clicked()), this, SLOT(load_existing_project_button_clicked()));
        connect(createProjectPushbutton, SIGNAL(clicked()), this, SLOT(create_new_project_button_clicked()));
        connect(&pm(), SIGNAL(currentProjectDirChanged()), this, SLOT(update_projects_combo_box()));
        connect(&pm(), SIGNAL(projectDirChangeDetected()), this, SLOT(update_projects_combo_box()));
        connect(&pm(), SIGNAL(projectsListChanged()), this, SLOT(update_projects_combo_box()));
        connect(&pm(), SIGNAL(projectLoaded(Project*)), this, SLOT(set_project(Project*)));
        connect(&pm(), SIGNAL(currentProjectDirChanged()), this, SLOT(update_projects_directory_line_edit()));
}


WelcomeWidget::~WelcomeWidget()
{

}

void WelcomeWidget::set_project(Project* project)
{
        if (project) {
                previousProjectLabel->setText(tr("Resume loaded"));
                loadPreviousProjectButton->setText(tr("Resume"));
                previousProjectLineEdit->setText(project->get_title());
        } else {
                previousProjectLabel->setText(tr("Load previous;"));
                loadPreviousProjectButton->setText(tr("Load"));

        }
}


void WelcomeWidget::load_existing_project_button_clicked()
{
        Project* project = pm().get_project();
        QString projectToLoad = projectsComboBox->currentText();


        if (project && (project->get_title() == projectToLoad)) {
                QMessageBox::StandardButton button = QMessageBox::question(this,
                        "Traverso - Question",
                        tr("Project '%1'' is already running \n\n"
                           "Do you really wish to reload it?").arg(projectToLoad),
                        QMessageBox::Ok | QMessageBox::Cancel,
                        QMessageBox::Cancel );
                        if (button == QMessageBox::Cancel) {
                        return;
                }
        }

        pm().load_project(projectToLoad);
}


void WelcomeWidget::load_previous_project_button_clicked()
{
        Project* project = pm().get_project();
        if (project) {
                TMainWindow::instance()->show_current_sheet();

        } else {

                QString previous = previousProjectLineEdit->text();
                if (previous.isEmpty() || previous.isNull()) {
                        return;
                }
                pm().load_project(previous);
        }
}

void WelcomeWidget::create_new_project_button_clicked()
{
        TMainWindow::instance()->show_newproject_dialog();
}

void WelcomeWidget::update_projects_combo_box()
{
        projectsComboBox->clear();

        foreach(QString project, pm().get_projects_list()) {
                projectsComboBox->addItem(project);
        }

        update_previous_project_line_edit();

}

void WelcomeWidget::update_previous_project_line_edit()
{
        QString current = config().get_property("Project", "current", "").toString();
        if (pm().project_exists(current)) {
                previousProjectLineEdit->setText(current);
        } else {
                previousProjectLineEdit->setText("");
        }

}

void WelcomeWidget::update_projects_directory_line_edit()
{
        QString path = pm().get_projects_directory();
        projectsDirLineEdit->setText(path);
}

void WelcomeWidget::on_changeProjectsDirButton_clicked()
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
                QMessageBox::information( this, tr("Traverso - Information"), tr("Created new Project directory for you here: %1\n").arg(newPath), QMessageBox::Ok);
        }

        pm().set_current_project_dir(newPath);

        update_projects_directory_line_edit();
}

void WelcomeWidget::keyPressEvent(QKeyEvent *event)
{
        if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
                if (loadExistingProjectButton->hasFocus() || projectsComboBox->hasFocus()) {
                        loadExistingProjectButton->animateClick();
                } else if (createProjectPushbutton->hasFocus()) {
                        createProjectPushbutton->animateClick();
                } else if (changeProjectsDirButton->hasFocus()) {
                        changeProjectsDirButton->animateClick();
                } else {
                        loadPreviousProjectButton->animateClick();
                }


                return;
        }

        QWidget::keyPressEvent(event);
}

//eof
