/*
Copyright (C) 2005-2006 Remon Sijrier

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

#include "ProjectManager.h"

#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileSystemWatcher>
#include <QTextStream>

#include "Project.h"
#include "Sheet.h"
#include "ContextPointer.h"
#include "ResourcesManager.h"
#include "Information.h"
#include "TInputEventDispatcher.h"
#include "TConfig.h"
#include "FileHelpers.h"
#include <AudioDevice.h>
#include <Utils.h>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"
#include "qthread.h"


/**	\class ProjectManager
	\brief ProjectManager is a singleton used for loading, creating and deleting Projects.
 
 
 */

ProjectManager::ProjectManager()
	: ContextItem()
{
	PENTERCONS;
        m_currentProject = (Project*) 0;
	m_exitInProgress = false;
	
	m_watcher = new QFileSystemWatcher(0);

	QString path = config().get_property("Project", "directory", "").toString();
	set_current_project_dir(path);
	
	cpointer().add_contextitem(this);
	
    connect(m_watcher, SIGNAL(directoryChanged(QString)), this, SLOT(project_dir_rename_detected(QString)));
}

/**
 * 	Used to get a reference to the ProjectManager
 * @return A reference to the ProjectManager singleton 
 */
ProjectManager& pm()
{
	static ProjectManager projMan;
	return projMan;
}

/**
 * 	The Resources Manager for the currently loaded Project

 * @return A pointer to the Resources Manager of the loaded Project, 0 if no Project is loaded
 */
ResourcesManager* resources_manager()
{
	Project* proj = pm().get_project();
	if (proj) {
		return proj->get_audiosource_manager();
	}
	return 0;
}

void ProjectManager::set_current_project(Project* project)
{
	PENTER;

        QString oldprojectname = "";
	
        if (m_currentProject) {

                ied().reject_current_hold_actions();

//                printf("exit in progress");
                QString oncloseaction = config().get_property("Project", "onclose", "save").toString();
                if (oncloseaction == "save") {
                        m_currentProject->save();
                } else if (oncloseaction == "ask") {
                        QMessageBox::StandardButton button =
                                QMessageBox::question(0,
                                tr("Save Project"),
                                tr("Should Project '%1' be safed before closing it?").arg(m_currentProject->get_title()),
                                QMessageBox::Yes | QMessageBox::No,
                                QMessageBox::Yes);
                        if (button  == QMessageBox::Yes) {
                                m_currentProject->save();
                        }
                }
		
                oldprojectname = m_currentProject->get_title();

                // this serves as a 'project closed' signal, emiting
                // a zero pointer as project makes the GUI to delete it's
                // project and releated GUI objects
                emit projectLoaded(nullptr);

                m_currentProject->set_project_closed();
        }

        m_currentProject = project;

        if (m_currentProject) {
                config().set_property("Project", "current", m_currentProject->get_title());
                emit projectLoaded(m_currentProject);
        } else if (!m_exitInProgress) {
                // free the audiodevice, but only if we don't want to quit.
                audiodevice().set_parameters(TAudioDeviceSetup());
        }

	if ( ! oldprojectname.isEmpty() ) {
		cleanup_backupfiles_for_project(oldprojectname);
	}

}

Project* ProjectManager::create_new_project(int numSheets, int numTracks, const QString& projectName)
{
	PENTER;

	if (project_exists(projectName)) {
		info().critical(tr("Project %1 already exists!").arg(projectName));
		return 0;
	}

	QString newrootdir = config().get_property("Project", "directory", "/directory/unknown/").toString() + "/" + projectName;
	m_projectDirs.append(newrootdir);
	
	Project* newProject = new Project(projectName);

	if (newProject->create(numSheets, numTracks) < 0) {
		delete newProject;
		info().critical(tr("Unable to create new Project %1").arg(projectName));
		return 0;
	}

        emit projectsListChanged();
	
	return newProject;
}

Project* ProjectManager::create_new_project(const QString& templatefile, const QString& projectName)
{
	if (project_exists(projectName)) {
		info().critical(tr("Project %1 already exists!").arg(projectName));
		return 0;
	}

	QString newrootdir = config().get_property("Project", "directory", "/directory/unknown/").toString() + "/" + projectName;
	m_projectDirs.append(newrootdir);
	
	Project* newProject = new Project(projectName);
	
	if (newProject->create(0, 0) < 0) {
		delete newProject;
		info().critical(tr("Unable to create new Project %1").arg(projectName));
		return 0;
	}
	
        if (newProject->save_from_template_to_project_file(templatefile, projectName) < 0) {
		return 0;
        }
	
        emit projectsListChanged();
	
	return newProject;
}

int ProjectManager::load_project(const QString& projectName)
{
	PENTER;

	if( ! project_exists(projectName) ) {
//		PERROR("project %s doesn't exist!", projectName.toLatin1().data());
		return -1;
	}

	Project* newProject = new Project(projectName);

	if (!newProject)
		return -1;

	set_current_project(newProject);

	int err;
        if ((err = m_currentProject->load()) < 0) {
		switch (err) {
			case Project::PROJECT_FILE_VERSION_MISMATCH: {
                                emit projectFileVersionMismatch(m_currentProject->get_root_dir(), m_currentProject->get_title());
				break;
			}
			default: {
                                emit projectLoadFailed(m_currentProject->get_title(), m_currentProject->get_error_string());
			}
		}
                info().critical(tr("Unable to load Project %1").arg(projectName));
                set_current_project(0);
		return -1;
        } else {
                m_currentProject->connect_to_audio_device();
        }
	
	return 1;
}

int ProjectManager::load_renamed_project(const QString & name)
{
        Q_ASSERT(m_currentProject);
	
	
	return load_project(name);
}


int ProjectManager::remove_project( const QString& name )
{
	// check if we are removing the currentProject, and delete it before removing its files
	if (project_is_current(name)) {
		PMESG("removing current project\n");
		set_current_project(0);
	}
	
	QString oldrootdir = config().get_property("Project", "directory", "/directory/unknown/").toString() + "/" + name;
	m_projectDirs.removeAll(oldrootdir);

        int r = FileHelper::remove_recursively( name );
        if (r == 1) {
                emit projectsListChanged();

        }

        return r;
}

bool ProjectManager::project_is_current(const QString& title)
{
	QString path = config().get_property("Project", "directory", "/directory/unknown").toString();
	path += "/" + title;

        if (m_currentProject && (m_currentProject->get_root_dir() == path)) {
		return true;
	}

	return false;
}

bool ProjectManager::project_exists(const QString& title)
{
	QString project_dir = config().get_property("Project", "directory", "/directory/unknown").toString();
	QString project_path = project_dir + "/" + title;
	QFileInfo fileInfo(project_path);

	if (fileInfo.exists()) {
		return true;
	}

	return false;
}

TCommand* ProjectManager::save_project()
{
        if (m_currentProject) {
                m_currentProject->save();
	} else {
		info().information( tr("No Project to save, open or create a Project first!"));
	}

	return (TCommand*) 0;
}

Project * ProjectManager::get_project( )
{
        return m_currentProject;
}


void ProjectManager::start(const QString & basepath, const QString & projectname)
{
	config().set_property("Project", "directory", basepath);
	
	if (project_exists(projectname)) {
		load_project(projectname);
	}
}


TCommand* ProjectManager::exit()
{
	PENTER;

    if (m_currentProject) {
        if (m_currentProject->get_sheets().size() == 0) {
            // No sheets to unregister from the audiodevice,
            // just save and quit:
            set_current_project(0);
            QApplication::exit();
            return 0;
        }

        if (m_currentProject->is_save_to_close()) {
            m_exitInProgress = true;
            set_current_project(0);
            QApplication::exit();
        } else {
            return 0;
        }
    } else {
        QApplication::exit();
    }


	return (TCommand*) 0;
}

void ProjectManager::scheduled_for_deletion( Sheet * sheet )
{
	PENTER;
	m_deletionSheetList.append(sheet);
}

void ProjectManager::delete_sheet( Sheet * sheet )
{
	PENTER;
	m_deletionSheetList.removeAll(sheet);
	delete sheet;
	
	if (m_deletionSheetList.isEmpty() && m_exitInProgress) {
		QApplication::exit();
	}
		
}

int ProjectManager::rename_project_dir(const QString & olddir, const QString & newdir)
{
	QDir dir(olddir);
	
	m_projectDirs.removeAll(olddir);
	m_projectDirs.append(newdir);
	
	if ( ! dir.rename(olddir, newdir)) {
		info().critical(tr("Could not rename Project directory to %1").arg(newdir));
		return - 1;
	}
	
	return 1;
}

void ProjectManager::set_current_project_dir(const QString & path)
{
	if (path.isEmpty()) {
		return;
	}
	
	QDir newdir(path);
	
	config().set_property("Project", "directory", newdir.canonicalPath());
	
	QStringList list = newdir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	m_projectDirs.clear();
	
	foreach(const QString &string, list) {
		m_projectDirs += path + "/" + string;
	}
	
	m_watcher->addPath(path);
	
        emit currentProjectDirChanged();
}

void ProjectManager::project_dir_rename_detected(const QString & dirname)
{
	Q_UNUSED(dirname);
	
	emit projectDirChangeDetected();
	
	QString path = config().get_property("Project", "directory", "").toString();
	QDir dir(path);
	
	QStringList list = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	
	bool startwhining = false;
	foreach(const QString &string, list) {
		if (!m_projectDirs.contains(path + "/" + string)) {
			startwhining = true;
			break;
		}
	}

	
	if (!startwhining) {
		return;
	}
	
	emit unsupportedProjectDirChangeDetected();
}

void ProjectManager::add_valid_project_path(const QString & path)
{
	m_projectDirs.append(path);
}

void ProjectManager::remove_wrong_project_path(const QString & path)
{
	m_projectDirs.removeAll(path);
}


void ProjectManager::start_incremental_backup(Project* project)
{
        if (!project) {
		return;
	}
	
        QString project_path = project->get_root_dir();
        QString fileName = project->get_root_dir() + "/project.tpf";
	QString backupdir = project_path + "/projectfilebackup";

	// Check if the projectfilebackup directory still exist
	QDir dir(backupdir);
	if (!dir.exists(backupdir)) {
		create_projectfilebackup_dir(project_path);
	}
	
	QFile reader(fileName);
	if (!reader.open(QIODevice::ReadOnly)) {
		info().warning(tr("Projectfile backup: The project file %1 could not be opened for reading (Reason: %2)").arg(fileName).arg(reader.errorString()));
		return;
	}
	
	QDateTime time = QDateTime::currentDateTime();
    QString writelocation = backupdir + "/" + time.toString() + "__" + QString::number(time.toMSecsSinceEpoch());
	QFile compressedWriter(writelocation);
	
	if (!compressedWriter.open( QIODevice::WriteOnly ) ) {
		compressedWriter.close();
                info().warning(tr("Projectfile backup: The project file %1 could not be opened for writing (Reason: %2)").arg(fileName).arg(compressedWriter.errorString()));
		return;
	}
	
	
	QByteArray array = reader.readAll();
	QByteArray compressed = qCompress(array, 9);
	QDataStream stream(&compressedWriter);
	stream << compressed;
	
	compressedWriter.close();
}


void ProjectManager::cleanup_backupfiles_for_project(const QString & projectname)
{
	if (! project_exists(projectname)) {
		return;
	}
	
	QString project_dir = config().get_property("Project", "directory", "/directory/unknown").toString();
	QString project_path = project_dir + "/" + projectname;
	QString backupdir = project_path + "/projectfilebackup";
	
	// Check if the projectfilebackup directory still exist
	QDir dir(backupdir);
	// A map to insert files based on their time value,
	// so it's sorted on date automatically
	QMap<int, QString> map;
	QStringList entrylist = dir.entryList(QDir::Files);
	
	// If there are more then 1000 saves, remove the last 200!
	if (entrylist.size() > 1000) {
		printf("more then thousand backup files, deleting oldest 200\n");
		
		int key;
		foreach (QString file, dir.entryList(QDir::Files)) {
			key = file.right(10).toUInt();
			map.insert(key, file);
		}
		
		QList<QString> tobedeleted = map.values();
		
		if (tobedeleted.size() < 201) {
			return;
		}

		for(int i=0; i<200; ++i) {
			QFile file(backupdir + "/" + tobedeleted.at(i));
			if ( ! file.remove() ) {
				printf("Could not remove file %s (Reason: %s)\n", QS_C(tobedeleted.at(i)), QS_C(FileHelper::fileerror_to_string(file.error())));
			}
		}
	}
}


int ProjectManager::restore_project_from_backup(const QString& projectname, uint restoretime)
{
	if (! project_exists(projectname)) {
		return -1;
	}
	QString project_dir = config().get_property("Project", "directory", "/directory/unknown").toString();
	QString project_path = project_dir + "/" + projectname;
	QString backupDir = project_path + "/projectfilebackup";
	
        if (m_currentProject) {
                m_currentProject->save();
		set_current_project(0);
	}

	QString fileName = project_path + "/project.tpf";
	
	QDir dir(backupDir);
	QString backupfile;
	
	foreach (QString backup, dir.entryList(QDir::Files)) {
		if (backup.right(10).toUInt() == restoretime) {
			backupfile = backupDir + "/" + backup;
			printf("backupfile %s\n", QS_C(backupfile));
			break;
		}
	}
	
	QFile reader(backupfile);
	if (!reader.open(QIODevice::ReadOnly)) {
		//
		reader.close();
		return -1;
	}
	
	
	QFile writer(fileName);
	if (!writer.open( QIODevice::WriteOnly | QIODevice::Text) ) {
//		PERROR("Could not open %s for writing!", QS_C(fileName));
		writer.close();
		return -1;
	}
	
	QDataStream dataIn(&reader);
	QByteArray compByteArray;
	dataIn >> compByteArray;
	
	QByteArray a = qUncompress(compByteArray);
	QTextStream stream(&writer);
	stream << a;
	
	writer.close();
	
	return 1;
}

QList< uint > ProjectManager::get_backup_date_times(const QString& projectname)
{
	if (! project_exists(projectname)) {
		return QList<uint>();
	}
	QString project_dir = config().get_property("Project", "directory", "/directory/unknown").toString();
	QString backupDir = project_dir + "/" + projectname + "/projectfilebackup";
	
	QList<uint> dateList;
	QDir dir(backupDir);
	
	foreach (QString filename, dir.entryList(QDir::Files)) {
		bool ok;
		uint date = filename.right(10).toUInt(&ok);
		if (ok) {
			dateList.append(date);
		} else {
			printf("filename: %s is not backupfile made by Traverso, removing it!\n", QS_C(filename));
			QFile::remove(backupDir + "/" + filename);
		}
	}

	return dateList;
}

int ProjectManager::create_projectfilebackup_dir(const QString& rootDir)
{
	QDir dir;
	QString path = rootDir + "/projectfilebackup/";

    if (!dir.mkdir(path)) {
		info().critical(tr("Cannot create dir %1").arg(path));
		return -1;
	}
	
	return 1;
}

TCommand* ProjectManager::close_current_project()
{
        set_current_project(nullptr);
        return nullptr;
}

QStringList ProjectManager::get_projects_list()
{
        QString path = config().get_property("Project", "directory", "none").toString();
        QDir dir(path);
        QStringList list = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QStringList projects;

        foreach(const QString &dirname, list) {
                QString fileToOpen = path + "/" + dirname + "/project.tpf";

                QFile file(fileToOpen);

                if (!file.open(QIODevice::ReadOnly)) {
                        PWARN(QString("ProjectManager:: Cannot open project properties file (%1)").arg(fileToOpen).toLatin1().data());
                        continue;
                }

                file.close();

                projects.append(dirname);
        }

        return projects;
}


QString ProjectManager::get_projects_directory()
{
        QString path = config().get_property("Project", "directory", "").toString();

        if (path.isEmpty() || path.isNull()) {
                path = QDir::homePath();
        }

        QDir dir;
        if (!dir.exists(path)) {
                path = QDir::homePath();
        }

        return path;
}

