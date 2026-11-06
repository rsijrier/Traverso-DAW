/*
Copyright (C) 2005-2024 Remon Sijrier

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

#include "TProjectManager.h"

#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileSystemWatcher>
#include <QTextStream>

#include <algorithm>

#include "TProject.h"
#include "TSheet.h"
#include "TContextPointer.h"
#include "TResourcesManager.h"
#include "TInformUser.h"
#include "TInputEventDispatcher.h"
#include "TConfig.h"
#include "TPeakCache.h"
#include "FileHelpers.h"
#include "TAudioDevice.h"
#include "TPeakBuildService.h"
#include "TBufferedAudioStreamReader.h"
#include "TResamplerSettings.h"
#include "TIOScheduler.h"
#include "TRtTransaction.h"
#include "Debugger.h"


/**	\class TProjectManager
	\brief TProjectManager is a singleton used for loading, creating and deleting Projects.
 
 
 */

TProjectManager::TProjectManager()
	: TContextItem()
{
	PENTERCONS;
    m_currentProject = nullptr;
	m_exitInProgress = false;
	
	m_watcher = new QFileSystemWatcher(0);

	QString path = config().get_property("Project", "directory", "").toString();
	set_current_project_dir(path);
	
	cpointer().add_contextitem(this);
	
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &TProjectManager::project_dir_rename_detected);

    // The peak cache, before any project asks for a peak file; and once
    // after the start, the cache within its limit
    apply_peak_cache_settings();
    connect(&config(), &TConfig::configChanged, this, &TProjectManager::apply_peak_cache_settings);
    TPeakCache::evict_later();
}

// Peaks/cachedir (empty: the platform's cache location) and
// Peaks/cachelimit (MB, 0: no limit)
void TProjectManager::apply_peak_cache_settings()
{
    QString directory = config().get_property("Peaks", "cachedir", "").toString();
    if (!directory.isEmpty()) {
        directory = QFileInfo(directory).absoluteFilePath();
    }
    TPeakCache::set_directory(directory);

    // MB; what is not a number, or below 0, is the default; at most 1 EB
    constexpr qint64 DefaultLimit = 10240;
    bool number = false;
    qint64 megabytes = config().get_property("Peaks", "cachelimit", DefaultLimit).toLongLong(&number);
    if (!number || megabytes < 0) {
        megabytes = DefaultLimit;
    }
    TPeakCache::set_limit(std::min<qint64>(megabytes, qint64(1) << 40) << 20);
}

/**
 * 	Used to get a reference to the TProjectManager
 * @return A reference to the TProjectManager singleton 
 */
TIOScheduler* TProjectManager::get_read_scheduler()
{
    Q_ASSERT_X(QThread::currentThread() == thread(), "TProjectManager::get_read_scheduler", "not on the GUI thread");
    if (!m_readScheduler) {
        // Half the cores for the idle work (peak builds), as the global pool
        // had before, the rest free for the GUI and the audio thread; and one
        // more, which idle work never takes: a refill finds a thread at once
        // (in rounds the scheduler thread took part, the dispatcher's never
        // runs a step)
        m_readScheduler = new TIOScheduler(std::max(QThread::idealThreadCount() / 2, 1) + 1);
        m_readScheduler->set_output_sample_rate(audiodevice().get_sample_rate());
        m_readScheduler->set_buffer_size(audiodevice().get_buffer_size());
        m_readScheduler->set_resampler(configured_resampler());
        m_readScheduler->start();
    }
    return m_readScheduler;
}

TIOScheduler* TProjectManager::get_write_scheduler()
{
    Q_ASSERT_X(QThread::currentThread() == thread(), "TProjectManager::get_write_scheduler", "not on the GUI thread");
    if (!m_writeScheduler) {
        // Two: one slow encoding step (WavPack) does not hold up the other
        // writers. A measurement of 32 tracks decides (dispatcher-conclusie.md).
        m_writeScheduler = new TIOScheduler(2);
        m_writeScheduler->set_buffer_size(audiodevice().get_buffer_size());
        m_writeScheduler->start();
    }
    return m_writeScheduler;
}

void TProjectManager::stop_schedulers()
{
    delete m_readScheduler;
    m_readScheduler = nullptr;
    delete m_writeScheduler;
    m_writeScheduler = nullptr;
}

TProjectManager& pm()
{
	static TProjectManager projMan;
	return projMan;
}

/**
 * 	The Resources Manager for the currently loaded Project

 * @return A pointer to the Resources Manager of the loaded Project, 0 if no Project is loaded
 */
TResourcesManager* resources_manager()
{
	TProject* proj = pm().get_project();
	if (proj) {
		return proj->get_audiosource_manager();
	}
	return 0;
}

void TProjectManager::set_current_project(TProject* project)
{
	PENTER;

        QString oldprojectname = "";
	
        if (m_currentProject) {

                ied().reject_current_hold_actions();

                // Recordings that were undone can not come back once the
                // project closes: discard them before it is saved
                m_currentProject->discard_undone_recordings();

//                printf("exit in progress");
                QString oncloseaction = config().get_property("Project", "onclose", "save").toString();
                if (m_closeWithoutQuestions && oncloseaction == "ask") {
                        oncloseaction = "save";
                }
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

                // The project closes: its peak builds are not needed anymore
                m_currentProject->get_peak_builds()->cancel_all();

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

TProject* TProjectManager::create_new_project(int numSheets, int numTracks, const QString& projectName)
{
	PENTER;

	if (project_exists(projectName)) {
		tInformUser().critical(tr("Project %1 already exists!").arg(projectName));
		return 0;
	}

	QString newrootdir = config().get_property("Project", "directory", "/directory/unknown/").toString() + "/" + projectName;
	m_projectDirs.append(newrootdir);
	
	TProject* newProject = nullptr;
	int result = 0;

	{
		// The whole new project reaches the audio thread as one change
		TRtTransaction transaction;

		newProject = new TProject(audiodevice(), projectName);
		result = newProject->create(numSheets, numTracks);
	}

	if (result < 0) {
		delete newProject;
		tInformUser().critical(tr("Unable to create new Project %1").arg(projectName));
		return 0;
	}

        emit projectsListChanged();
	
	return newProject;
}

TProject* TProjectManager::create_new_project(const QString& templatefile, const QString& projectName)
{
	if (project_exists(projectName)) {
		tInformUser().critical(tr("Project %1 already exists!").arg(projectName));
		return 0;
	}

	QString newrootdir = config().get_property("Project", "directory", "/directory/unknown/").toString() + "/" + projectName;
	m_projectDirs.append(newrootdir);
	
	TProject* newProject = nullptr;
	int result = 0;

	{
		// The whole new project reaches the audio thread as one change
		TRtTransaction transaction;

		newProject = new TProject(audiodevice(), projectName);
		result = newProject->create(0, 0);
	}

	if (result < 0) {
		delete newProject;
		tInformUser().critical(tr("Unable to create new Project %1").arg(projectName));
		return 0;
	}
	
        if (newProject->save_from_template_to_project_file(templatefile, projectName) < 0) {
		return 0;
        }
	
        emit projectsListChanged();
	
	return newProject;
}

int TProjectManager::load_project(const QString& projectName)
{
	PENTER;

	if( ! project_exists(projectName) ) {
//		PERROR("project %s doesn't exist!", projectName.toLatin1().data());
		return -1;
	}

	int err = 0;

	TProject* newProject = new TProject(audiodevice(), projectName);

	// Outside the transaction: closing the current project removes its
	// client from the audio device, which must be applied by the audio
	// thread before the project is deleted. Inside a transaction the
	// removal would wait for the transaction to end.
	set_current_project(newProject);

	{
		// The whole project reaches the audio thread as one change, applied in
		// one audio cycle. Load() starts the project's audio driver, so the
		// change is published to it.
		TRtTransaction transaction;

		err = m_currentProject->load();
	}

        if (err < 0) {
		switch (err) {
			case TProject::PROJECT_FILE_VERSION_MISMATCH: {
                                emit projectFileVersionMismatch(m_currentProject->get_root_dir(), m_currentProject->get_title());
				break;
			}
			default: {
                                emit projectLoadFailed(m_currentProject->get_title(), m_currentProject->get_error_string());
			}
		}
                tInformUser().critical(tr("Unable to load Project %1").arg(projectName));
                set_current_project(0);
		return -1;
        } else {
                m_currentProject->connect_to_audio_device();
        }
	
	return 1;
}

int TProjectManager::load_renamed_project(const QString & name)
{
        Q_ASSERT(m_currentProject);
	
	
	return load_project(name);
}


int TProjectManager::remove_project( const QString& name )
{
	// check if we are removing the currentProject, and delete it before removing its files
	if (project_is_current(name)) {
		PMESG("removing current project\n");
		set_current_project(0);
	}
	
	QString oldrootdir = config().get_property("Project", "directory", "/directory/unknown/").toString() + "/" + name;
	m_projectDirs.removeAll(oldrootdir);

        int r = TFileHelper::remove_recursively( name );
        if (r == 1) {
                emit projectsListChanged();

        }

        return r;
}

bool TProjectManager::project_is_current(const QString& title)
{
	QString path = config().get_property("Project", "directory", "/directory/unknown").toString();
	path += "/" + title;

        if (m_currentProject && (m_currentProject->get_root_dir() == path)) {
		return true;
	}

	return false;
}

bool TProjectManager::project_exists(const QString& title)
{
	QString project_dir = config().get_property("Project", "directory", "/directory/unknown").toString();
	QString project_path = project_dir + "/" + title;
	QFileInfo fileInfo(project_path);

	if (fileInfo.exists()) {
		return true;
	}

	return false;
}

TCommand* TProjectManager::save_project()
{
        if (m_currentProject) {
                m_currentProject->save();
	} else {
		tInformUser().information( tr("No Project to save, open or create a Project first!"));
	}

	return (TCommand*) 0;
}

TProject * TProjectManager::get_project( )
{
        return m_currentProject;
}


void TProjectManager::start(const QString & basepath, const QString & projectname)
{
	config().set_property("Project", "directory", basepath);
	
	if (project_exists(projectname)) {
		load_project(projectname);
	}
}


TCommand* TProjectManager::exit()
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

void TProjectManager::exit_without_questions()
{
    m_closeWithoutQuestions = true;
    exit();
}

void TProjectManager::scheduled_for_deletion( TSheet * sheet )
{
	PENTER;
	m_deletionSheetList.append(sheet);
}

void TProjectManager::delete_sheet( TSheet * sheet )
{
	PENTER;
	m_deletionSheetList.removeAll(sheet);
	delete sheet;
	
	if (m_deletionSheetList.isEmpty() && m_exitInProgress) {
		QApplication::exit();
	}
		
}

int TProjectManager::rename_project_dir(const QString & olddir, const QString & newdir)
{
	QDir dir(olddir);
	
	m_projectDirs.removeAll(olddir);
	m_projectDirs.append(newdir);
	
	if ( ! dir.rename(olddir, newdir)) {
		tInformUser().critical(tr("Could not rename Project directory to %1").arg(newdir));
		return - 1;
	}
	
	return 1;
}

void TProjectManager::set_current_project_dir(const QString & path)
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

void TProjectManager::project_dir_rename_detected(const QString & dirname)
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

void TProjectManager::add_valid_project_path(const QString & path)
{
	m_projectDirs.append(path);
}

void TProjectManager::remove_wrong_project_path(const QString & path)
{
	m_projectDirs.removeAll(path);
}


void TProjectManager::start_incremental_backup(TProject* project)
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
		tInformUser().warning(tr("Projectfile backup: The project file %1 could not be opened for reading (Reason: %2)").arg(fileName).arg(reader.errorString()));
		return;
	}
	
	QDateTime time = QDateTime::currentDateTime();
    QString writelocation = backupdir + "/" + time.toString() + "__" + QString::number(time.toMSecsSinceEpoch());
	QFile compressedWriter(writelocation);
	
	if (!compressedWriter.open( QIODevice::WriteOnly ) ) {
		compressedWriter.close();
                tInformUser().warning(tr("Projectfile backup: The project file %1 could not be opened for writing (Reason: %2)").arg(fileName).arg(compressedWriter.errorString()));
		return;
	}
	
	
	QByteArray array = reader.readAll();
	QByteArray compressed = qCompress(array, 9);
	QDataStream stream(&compressedWriter);
	stream << compressed;
	
	compressedWriter.close();
}


void TProjectManager::cleanup_backupfiles_for_project(const QString & projectname)
{
    if (! project_exists(projectname)) {
        return;
    }

    QString project_dir = config().get_property("Project", "directory", "/directory/unknown").toString();
    QString project_path = project_dir + "/" + projectname;
    QString backupdir = project_path + "/projectfilebackup";

    QDir dir(backupdir);
    QMap<qint64, QString> map;
    QStringList entrylist = dir.entryList(QDir::Files);

    if (entrylist.size() > 1000) {
        TLOG_I("project", "more than a thousand backup files, deleting the oldest 200");

        foreach (QString file, dir.entryList(QDir::Files)) {
            int separatorPos = file.lastIndexOf("__");
            if (separatorPos != -1) {
                qint64 key = file.mid(separatorPos + 2).toLongLong();
                map.insert(key, file);
            }
        }

        QList<QString> tobedeleted = map.values();
        if (tobedeleted.size() < 201) {
            return;
        }

        for(int i=0; i<200; ++i) {
            QFile file(backupdir + "/" + tobedeleted.at(i));
            if ( ! file.remove() ) {
                TLOG_W("project", "could not remove {} ({})", tobedeleted.at(i), TFileHelper::fileerror_to_string(file.error()));
            }
        }
    }
}



int TProjectManager::restore_project_from_backup(const QString& projectname, qint64 restoretime)
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
        int separatorPos = backup.lastIndexOf("__");
        if (separatorPos != -1) {
            qint64 fileTime = backup.mid(separatorPos + 2).toLongLong();
            if (fileTime == restoretime) {
                backupfile = backupDir + "/" + backup;
                TLOG_D("project", "backup file found: {}", backupfile);
                break;
            }
        }
    }

    QFile reader(backupfile);
    if (!reader.open(QIODevice::ReadOnly)) {
        reader.close();
        return -1;
    }

    QFile writer(fileName);
    if (!writer.open( QIODevice::WriteOnly | QIODevice::Text) ) {
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


QList<qint64> TProjectManager::get_backup_date_times(const QString& projectname)
{
    if (! project_exists(projectname)) {
        return QList<qint64>();
    }
    QString project_dir = config().get_property("Project", "directory", "/directory/unknown").toString();
    QString backupDir = project_dir + "/" + projectname + "/projectfilebackup";

    QList<qint64> dateList;
    QDir dir(backupDir);

    foreach (QString filename, dir.entryList(QDir::Files)) {
        bool ok = false;
        qint64 date = 0;

        int separatorPos = filename.lastIndexOf("__");
        if (separatorPos != -1) {
            date = filename.mid(separatorPos + 2).toLongLong(&ok);
        }

        if (ok) {
            dateList.append(date);
        } else {
            TLOG_W("project", "{} is not a backup file made by Traverso, removing it", filename);
            QFile::remove(backupDir + "/" + filename);
        }
    }

    return dateList;
}


int TProjectManager::create_projectfilebackup_dir(const QString& rootDir)
{
	QDir dir;
	QString path = rootDir + "/projectfilebackup/";

    if (!dir.mkdir(path)) {
		tInformUser().critical(tr("Cannot create dir %1").arg(path));
		return -1;
	}
	
	return 1;
}

TCommand* TProjectManager::close_current_project()
{
        set_current_project(nullptr);
        return nullptr;
}

QStringList TProjectManager::get_projects_list()
{
        QString path = config().get_property("Project", "directory", "none").toString();
        QDir dir(path);
        QStringList list = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QStringList projects;

        foreach(const QString &dirname, list) {
                QString fileToOpen = path + "/" + dirname + "/project.tpf";

                QFile file(fileToOpen);

                if (!file.open(QIODevice::ReadOnly)) {
                        TLOG_D("project", "not a project directory, no properties file: {}", fileToOpen);
                        continue;
                }

                file.close();

                projects.append(dirname);
        }

        return projects;
}


QString TProjectManager::get_projects_directory()
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

