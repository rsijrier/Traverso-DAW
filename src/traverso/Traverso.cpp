/*
Copyright (C) 2005-2026 Remon Sijrier
This file is part of Traverso
*/

#include <csignal>
#include <cstdlib>

#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QStyleFactory>

#include "Traverso.h"
#include "TBufferOperations.h"
#include "TAudioThreadMessageQueue.h"
#include "TInformUser.h"
#include "TProjectManager.h"
#include "TTransport.h"
#include "TMainWindow.h"
#include "TThemer.h"
#include "TConfig.h"
#include "TAudioDevice.h"
#include "TAudioDeviceSetup.h"
#include "TContextPointer.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TSheet.h"
#include "TLog.h"
#include "Debugger.h"
#include "TDenormals.h"
#include "TObjectIdTags.h"

#include <QTimer>

#include <csignal>


Traverso::Traverso(int &argc, char **argv )
    : QApplication ( argc, argv )
{
    QCoreApplication::setOrganizationName("Traverso");
    QCoreApplication::setApplicationName("Traverso");
    QCoreApplication::setOrganizationDomain("traversodaw.com");

    qRegisterMetaType<TInformUserData>("InfoStruct");
    qRegisterMetaType<TTimeRef>("TTimeRef");
    qRegisterMetaType<QList<QObject*>>("QList<QObject*>");
    register_object_id_names();

    tsmp();
    m_audioDevice = std::make_unique<TAudioDevice>();
    config().check_and_load_configuration();
    srand ( time(nullptr) );

    // Before any other thread: the threads Traverso starts inherit the bits;
    // the audio threads get them every cycle too (TDenormals)
    TDenormals::enforce();
    TLOG_I("app", "Buffer operations: {}; denormals: {}", TBufferOperations::implementation_name(), TDenormals::mode_name());

    // The audio thread runs from the start: its period is the clock pulse of
    // Traverso, and real-time changes are only ever posted to it. Loading a
    // project switches to the driver configured for it.
    TAudioDeviceSetup dummySetup;
    dummySetup.set_driver_type("Dummy");
    audiodevice().set_parameters(dummySetup);

    connect(this, &QApplication::lastWindowClosed, &pm(), &TProjectManager::exit);
}

Traverso::~Traverso()
{
    PENTERDES;
    // The last project went when exec() returned (its deleteLater()): the
    // application's schedulers end now
    pm().stop_schedulers();
    audiodevice().shutdown();
    if (TMainWindow::has_instance()) {
        delete TMainWindow::instance();
    }
    delete themer();
    config().save();
}

void Traverso::create_interface()
{
    themer()->load();

    cpointer().add_contextitem(&transport());

    TMainWindow* tMainWindow = TMainWindow::instance();
    tMainWindow->show();

    QString projectToLoad = "";

    foreach(QString string, QCoreApplication::arguments ()) {
        if (string.contains("project.tpf")) {
            projectToLoad = string;
            break;
        }
    }

    if (!projectToLoad.isEmpty()) {
        QFileInfo fi(projectToLoad);
        QDir projectdir(fi.path());
        QDir baseprojectdir(fi.path());
        baseprojectdir.cdUp();
        QString baseprojectdirpath = baseprojectdir.path();
        QString projectdirpath = projectdir.path();
        QString projectname = projectdirpath.mid(baseprojectdirpath.length() + 1, projectdirpath.length());

        if (!projectname.isEmpty() && !baseprojectdirpath.isEmpty()) {
            pm().start(baseprojectdirpath, projectname);
            return;
        }
    }
    else {
        if (config().get_property("Project", "welcome", "welcome").toString() == "restore") {
            QString previous = config().get_property("Project", "current", "").toString();
            if (!previous.isEmpty() && !previous.isNull()) {
                if (pm().project_exists(previous)) {
                    pm().load_project(previous);
                }
            }
        }
    }
}

// SIGINT (Ctrl+C) closes as the window does: the onclose setting, "ask"
// included, and not while recording. SIGTERM (kill, logout, system
// shutdown) and SIGHUP (the terminal closed) close soon and without
// questions, since nobody may be there to answer: see
// close_without_questions()
void Traverso::close_on_signal(int signal)
{
    if (signal == SIGINT) {
        TLOG_I("app", "closing on SIGINT, as the window closes");
        pm().exit();
        return;
    }
    TLOG_I("app", "closing on {}, without questions", signal == SIGTERM ? "SIGTERM" : "SIGHUP");
    close_without_questions();
}

// Stops a recording and waits until its takes are written (a close right
// after the stop would lose their last part), then closes; "ask" saves
void Traverso::close_without_questions()
{
    bool waiting = false;
    if (TProject* project = pm().get_project()) {
        for (TSheet* sheet : project->get_sheets()) {
            if (sheet->is_recording() && sheet->is_transport_rolling()) {
                // Rolling: this stops it
                sheet->start_transport();
            }
            waiting = waiting || (sheet->is_recording() && sheet->is_transport_rolling()) || sheet->has_unfinished_takes();
        }
    }
    if (waiting) {
        QTimer::singleShot(20, this, &Traverso::close_without_questions);
        return;
    }
    pm().exit_without_questions();
}

void Traverso::saveState(QSessionManager &manager)
{
    manager.setRestartHint(QSessionManager::RestartIfRunning);
    QStringList command;
    command << "traverso" << "-session" << QApplication::sessionId();
    manager.setRestartCommand(command);
}

void Traverso::commitData(QSessionManager &)
{
    pm().save_project();
}
