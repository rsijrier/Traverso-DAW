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
#include "Mixer.h"
#include "TAudioThreadMessageQueue.h"
#include "TInformUser.h"
#include "TProjectManager.h"
#include "TTransport.h"
#include "TMainWindow.h"
#include "TThemer.h"
#include "TConfig.h"
#include "TAudioDevice.h"
#include "TContextPointer.h"
#include "Debugger.h"
#include "fpu.h"

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#define TRAVERSO_HAS_XMMINTRIN 1
#endif

Traverso::Traverso(int &argc, char **argv )
    : QApplication ( argc, argv )
{
    QCoreApplication::setOrganizationName("Traverso");
    QCoreApplication::setApplicationName("Traverso");
    QCoreApplication::setOrganizationDomain("traversodaw.com");

    qRegisterMetaType<TInformUserData>("InfoStruct");
    qRegisterMetaType<TTimeRef>("TTimeRef");
    qRegisterMetaType<QList<QObject*>>("QList<QObject*>");

    tsmp();
    config().check_and_load_configuration();
    srand ( time(nullptr) );

    init_sse();

    connect(this, &QApplication::lastWindowClosed, &pm(), &TProjectManager::exit);
}

Traverso::~Traverso()
{
    PENTERDES;
    audiodevice().shutdown();
    delete TMainWindow::instance();
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

void Traverso::shutdown(int signal)
{
    PENTER;

    cpointer().hold_finished();
    QApplication::processEvents();

    switch(signal) {
    case SIGINT:
        printf("\nCaught the SIGINT signal!\nShutting down Traverso!\n\n");
        pm().exit();
        return;
    case SIGSEGV:
        printf("\nCaught the SIGSEGV signal!\n");
        QMessageBox::critical(TMainWindow::instance(), "Crash",
                              "The program made an invalid operation and crashed :-(\n"
                              "Please, report this to us!");
    }

    printf("Stopped\n");
    exit(0);
}

void Traverso::init_sse()
{
    bool generic_mix_functions = true;
    FPU fpu;

#if defined(__SSE__) && defined(SSE_OPTIMIZATIONS)
    if (fpu.has_sse()) {
        printf("Using SSE optimized routines\n");

        Mixer::compute_peak           = x86_sse_compute_peak;
        Mixer::apply_gain_to_buffer   = x86_sse_apply_gain_to_buffer;
        Mixer::mix_buffers_with_gain  = x86_sse_mix_buffers_with_gain;
        Mixer::mix_buffers_no_gain    = x86_sse_mix_buffers_no_gain;

        generic_mix_functions = false;
    }
#elif defined (__APPLE__)
    Mixer::compute_peak           = accel_compute_peak;
    Mixer::apply_gain_to_buffer   = accel_apply_gain_to_buffer;
    Mixer::mix_buffers_with_gain  = accel_mix_buffers_with_gain;
    Mixer::mix_buffers_no_gain    = accel_mix_buffers_no_gain;
    generic_mix_functions = false;

    printf("Apple Accelerate/vDSP H/W specific optimizations in use\n");
#endif

    setup_fpu();

    if (generic_mix_functions) {
        Mixer::compute_peak           = default_compute_peak;
        Mixer::apply_gain_to_buffer   = default_apply_gain_to_buffer;
        Mixer::mix_buffers_with_gain  = default_mix_buffers_with_gain;
        Mixer::mix_buffers_no_gain    = default_mix_buffers_no_gain;

        printf("No Hardware specific optimizations in use\n");
    }
}

void Traverso::setup_fpu()
{
    if (getenv("TRAVERSO_RUNNING_UNDER_VALGRIND")) {
        printf("TRAVERSO_RUNNING_UNDER_VALGRIND=TRUE\n");
        return;
    }

#if defined(TRAVERSO_HAS_XMMINTRIN) && defined(SSE_OPTIMIZATIONS)
    FPU fpu;

    if (!fpu.has_flush_to_zero() && !fpu.has_denormals_are_zero()) {
        return;
    }

    int MXCSR = _mm_getcsr();

    MXCSR &= ~_MM_FLUSH_ZERO_ON;
    if (fpu.has_denormals_are_zero()) {
        MXCSR |= 0x8000;
    }

    _mm_setcsr (MXCSR);
#endif
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
