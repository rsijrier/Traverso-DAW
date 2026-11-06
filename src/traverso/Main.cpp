/*
Copyright (C) 2005-2009 Remon Sijrier 

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

#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <algorithm>

#include <QFile>
#include <QLocale>
#include <QTextStream>
#include <QTranslator>

#if defined (Q_OS_LINUX) || defined (Q_OS_MAC)
#include <QSocketNotifier>
#include <sys/resource.h>
#include <sys/socket.h>
#endif
#if defined (Q_OS_MAC)
#include <sys/syslimits.h>
#endif

#include "TConfig.h"
#include "Traverso.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TMainWindow.h"
#include "TShortCutManager.h"
#include "Debugger.h"
#include "TLog.h"

Traverso* traverso;


#if defined (Q_OS_LINUX) || defined (Q_OS_MAC)
// SIGINT, SIGTERM and SIGHUP close Traverso (Traverso::close_on_signal()).
// A signal handler may do next to nothing: it writes the signal into a
// socket pair, the GUI thread reads it there. A second signal stops at
// once, for a close that does not end.
static int signalSockets[2] = {-1, -1};
static volatile sig_atomic_t signalCount = 0;

static void catch_signal(int signalNumber)
{
	if (signalCount == 0) {
		signalCount = 1;
		const unsigned char number = static_cast<unsigned char>(signalNumber);
		[[maybe_unused]] const ssize_t written = write(signalSockets[0], &number, 1);
		static const char closing[] = "\nClosing Traverso; a second signal stops it at once\n";
		[[maybe_unused]] const ssize_t told = write(STDERR_FILENO, closing, sizeof(closing) - 1);
		return;
	}
	static const char stopping[] = "Second signal: Traverso stops at once\n";
	[[maybe_unused]] const ssize_t told = write(STDERR_FILENO, stopping, sizeof(stopping) - 1);
	_exit(1);
}

// Only these: a crash must stay a crash (core dump, debugger stop)
static void catch_close_signals()
{
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, signalSockets) != 0) {
		perror("Traverso: no socket pair for SIGINT, SIGTERM and SIGHUP");
		return;
	}
	struct sigaction action = {};
	action.sa_handler = catch_signal;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESTART;
	for (int signalNumber : {SIGINT, SIGTERM, SIGHUP}) {
		sigaction(signalNumber, &action, nullptr);
	}
}
#endif


int main( int argc, char **argv )
{
#if defined (Q_OS_LINUX) || defined (Q_OS_MAC)
	catch_close_signals();
#endif

	// The session log is always written; stderr too in developer builds
	QString logDirectory = TLog::default_directory();
#ifdef TRAVERSO_DEV_LOG
	bool logToStderr = true;
#else
	bool logToStderr = false;
#endif
	if (argc > 1) {
		for (int i=1; i<argc; i++) {
			if (strncmp(argv[i], "--log-level=", 12) == 0) {
				TLogLevel level;
				if (TLog::level_from_name(argv[i] + 12, level)) {
					TLog::set_level(level);
				} else {
					printf("Unknown log level '%s', use error, warning, info, debug or trace\n", argv[i] + 12);
					return 1;
				}
			}
			if (strcmp(argv[i],"--log-stderr")==0)
					logToStderr = true;
			if (strcmp(argv[i],"--no-log-stderr")==0)
					logToStderr = false;
			if (strncmp(argv[i], "--log-dir=", 10) == 0)
					logDirectory = QString::fromLocal8Bit(argv[i] + 10);
			if ((strcmp(argv[i],"--help")==0) || (strcmp(argv[i],"-h")==0)) {
				printf("\nUsage: traverso [OPTIONS]\n\n");
				printf("Valid OPTIONS are :\n\n");
				printf("\t--help or -h \t This help text\n");
				printf("\t -v   \t\t Print version number\n");
				printf("\t--log-level=LEVEL\t error, warning, info (default), debug or trace (the last two in developer builds)\n");
				printf("\t--log-stderr / --no-log-stderr\t Also write the log to stderr (default in developer builds)\n");
				printf("\t--log-dir=DIR\t\t Directory of traverso.log (default: ~/.local/state/Traverso, ~/Library/Logs/Traverso or AppData/Local/Traverso/logs)\n");
				printf("\t--show-compile-options\t\t Print options used during compilation\n");
                                printf("\t--fft-meter   \t\t Start Traverso as a Spectral Analyzer\n");
                                printf("\t--dump-shortcut-table FILE\t Write the shortcut resolution table to FILE and quit\n");
                                printf("\n");
				return 0;
			}
			if (strcmp(argv[i],"-v")==0) {
                printf("Traverso %s\n", VERSION);
				return 0;
			}
			if (strcmp(argv[i],"--show-compile-options")==0) {
                // printf("Traverso compile options: %s\n", TRAVERSO_DEFINES);
				return 0;
			}
		}
	}
	// Before the application: Qt's messages while it starts go to the log too
	TLog::name_thread("gui");
	TLog::start(logDirectory, logToStderr, VERSION);
	TLOG_I("app", "log file {}", TLog::file_name());

#if defined (Q_OS_LINUX) || defined (Q_OS_MAC)
	// Every audio clip keeps its file open: take the open files the system allows
	struct rlimit openFiles;
	if (getrlimit(RLIMIT_NOFILE, &openFiles) == 0 && openFiles.rlim_cur < openFiles.rlim_max) {
		rlim_t wanted = openFiles.rlim_max;
#if defined (Q_OS_MAC)
		wanted = std::min<rlim_t>(wanted, OPEN_MAX);
#endif
		const rlim_t before = openFiles.rlim_cur;
		openFiles.rlim_cur = wanted;
		if (setrlimit(RLIMIT_NOFILE, &openFiles) == 0) {
			TLOG_I("app", "open files limit raised from {} to {}", (unsigned long long)before, (unsigned long long)wanted);
		} else {
			TLOG_W("app", "cannot raise the open files limit from {}", (unsigned long long)before);
		}
	}
#endif

	traverso = new Traverso(argc, argv);

	// Writes the shortcut resolution table for both built-in keymaps and quits,
	// used to verify that shortcut handling keeps behaving the same. It quits
	// the way an ordinary exit does: the audio thread runs from the start and
	// must stop before the statics it uses are gone
	for (int i=1; i<argc-1; i++) {
		if (strcmp(argv[i],"--dump-shortcut-table")==0) {
			int result = 0;
			QFile file(QString::fromLocal8Bit(argv[i+1]));
			if (file.open(QFile::WriteOnly | QFile::Truncate)) {
				QTextStream out(&file);
				QVariant simplified = config().get_property("InputEventDispatcher", "UseSimplifiedShortcuts", true);
				for (bool useSimplified : {true, false}) {
					config().set_property("InputEventDispatcher", "UseSimplifiedShortcuts", useSimplified);
					tShortCutManager().load_shortcuts();
					out << "## KEYMAP " << (useSimplified ? "simplified" : "advanced") << "\n";
					tShortCutManager().write_resolution_table(out);
				}
				config().set_property("InputEventDispatcher", "UseSimplifiedShortcuts", simplified);
			} else {
				TLOG_E("app", "cannot write the shortcut table to {}", argv[i+1]);
				result = 1;
			}
			delete traverso;
			TLog::stop();
			return result;
		}
	}
	
    QTranslator traversoTranslator;
	QString systemLanguage = QLocale::system().name();
	QString userLanguage = config().get_property("Interface", "LanguageFile", "").toString();
    bool translaterLoaded = false;
	if (userLanguage.isEmpty() || userLanguage.isNull()) {
        translaterLoaded = traversoTranslator.load(":/translations/traverso_" + systemLanguage );
	} else {
        translaterLoaded = traversoTranslator.load(userLanguage);
	}

    if (translaterLoaded) {
        traverso->installTranslator(&traversoTranslator);
    }

        traverso->create_interface();

        if (argc > 1) {
                for (int i=1; i<argc; i++) {
                        if (strcmp(argv[i],"--fft-meter")==0) {
                                printf("Using Traverso in FFT Meter only mode\n");
                                if (!pm().project_exists("fft-meter")) {
                                        TProject* project = pm().create_new_project(1, 1, "fft-meter");
                                        project->save();
                                        delete project;

                                }
                                pm().load_project("fft-meter");
                                TMainWindow::instance()->show_fft_meter_only();
                        }
                }
        }

#if defined (Q_OS_LINUX) || defined (Q_OS_MAC)
	// The GUI thread's end of catch_signal(); a signal that came while
	// Traverso started waits in the socket until now
	if (signalSockets[1] >= 0) {
		auto* signalNotifier = new QSocketNotifier(signalSockets[1], QSocketNotifier::Read, traverso);
		QObject::connect(signalNotifier, &QSocketNotifier::activated, traverso, []() {
			unsigned char number = 0;
			if (read(signalSockets[1], &number, 1) == 1) {
				traverso->close_on_signal(number);
			}
		});
	}
#endif

	traverso->exec();
	
	delete traverso;

	TLOG_I("app", "Thank you for using Traverso !");
	TLog::stop();
	return 0;
}

