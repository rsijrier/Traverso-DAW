/*
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

#include "TMainWindow.h"


#include "TAudioClip.h"
#include "TAudioClipView.h"
#include "TInformUser.h"
#include "TTimeLineMarker.h"
#include "PlayHeadMove.h"
#include "TProject.h"
#include "TBufferedAudioStreamReader.h"
#include "ResampleAudioReader.h"
#include "TSheet.h"
#include "TTrack.h"
#include "TVUMonitor.h"
#include "TSheetView.h"
#include "TShortCutFunction.h"
#include "TTrack.h"
#include "TBusTrack.h"
#include "TVUMonitor.h"
#include "TShortCutFunction.h"
#include "TTrack.h"
#include "TVUMonitor.h"
#include "TShortCutManager.h"
#include "TInputEventDispatcher.h"
#include "TContextPointer.h"

#include <QDockWidget>
#include <QUndoView>
#include <QFile>
#include <QDir>
#include <QMenuBar>
#include <QMessageBox>
#include <QDesktopServices>
#include <QTextStream>
#include <QFileDialog>
#include <QFileInfo>
#include <QTabBar>
#include <QCompleter>
#include <QStandardItemModel>

#include "TProjectManager.h"
#include "TTrackView.h"
#include "TClipsViewPort.h"
#include "TFadeCurve.h"
#include "TConfig.h"
#include "TAudioPlugin.h"
#include "TAudioClipFactory.h"
#include "TUndoCommand.h"
#include "TTimeLineRuler.h"
#include "TThemer.h"
#include "TAudioFileCopyConvert.h"
#include "ClipTileCache.h"

#include "../sheetcanvas/TSheetWidget.h"

#include "qundogroup.h"
#include "ui_QuickStart.h"

#include "widgets/TAudioBusVUMonitorWidget.h"
#include "widgets/InfoWidgets.h"
#include "widgets/ResourcesWidget.h"
#include "widgets/CorrelationMeterWidget.h"
#include "widgets/SpectralMeterWidget.h"
#include "widgets/TransportConsoleWidget.h"
#include "widgets/WelcomeWidget.h"
#include "widgets/TSessionTabWidget.h"
#include "widgets/TContextHelpWidget.h"

#include "dialogs/settings/SettingsDialog.h"
#include "dialogs/project/ProjectManagerDialog.h"
#include "dialogs/project/OpenProjectDialog.h"
#include "dialogs/project/NewProjectDialog.h"
#include "dialogs/project/NewSheetDialog.h"
#include "dialogs/project/NewTrackDialog.h"
#include "dialogs/MarkerDialog.h"
#include "dialogs/InsertSilenceDialog.h"
#include "dialogs/RestoreProjectBackupDialog.h"
#include "dialogs/ProjectConverterDialog.h"
#include "dialogs/ExportDialog.h"
#include "dialogs/CDWritingDialog.h"
#include "dialogs/project/ImportClipsDialog.h"
#include "dialogs/TTrackSelector.h"
#include "dialogs/TShortcutEditorDialog.h"



#include "Debugger.h"


class HistoryWidget : public QUndoView
{
public:
    HistoryWidget(QUndoGroup* group, QWidget* parent);
    ~HistoryWidget();

protected:
    QSize sizeHint() const {
        return QSize(120, 140);
    }
    QSize minimumSizeHint() const
    {
        return QSize(90, 90);
    }
};

HistoryWidget::HistoryWidget(QUndoGroup *group, QWidget *parent)
    : QUndoView(group, parent)
{
}

HistoryWidget::~HistoryWidget()
{

}


TMainWindow* TMainWindow::m_instance = nullptr;

TMainWindow* TMainWindow::instance()
{
    if (m_instance == nullptr) {
        m_instance = new TMainWindow();
    }

    return m_instance;
}

TMainWindow::TMainWindow()
    : QMainWindow()
{
    PENTERCONS;

    m_instance = this;

    setWindowTitle("Traverso");
    setMinimumSize(400, 300);
    setWindowIcon(QPixmap(":/ui/app/app-logo.svg"));

    // Track finder and related
    m_trackFinder = new QLineEdit(this);
    m_trackFinder->setMinimumWidth(100);
    m_trackFinder->installEventFilter(this);
    m_trackFinder->setCompleter(&m_trackFinderCompleter);
    m_trackFinderCompleter.setModel(&m_trackFinderModel);
    m_trackFinderCompleter.setCaseSensitivity(Qt::CaseInsensitive);
    m_trackFinder->setCompleter(&m_trackFinderCompleter);
    connect(&m_trackFinderCompleter,
            QOverload<const QModelIndex &>::of(&QCompleter::activated),
            this, &TMainWindow::track_finder_model_index_changed);
    connect(m_trackFinder, &QLineEdit::returnPressed, this, &TMainWindow::track_finder_return_pressed);

    m_trackFinderTreeView = new QTreeView;
    m_trackFinderTreeView->setMinimumWidth(250);
    m_trackFinderCompleter.setPopup(m_trackFinderTreeView);
    m_trackFinderTreeView->setRootIsDecorated(false);
    m_trackFinderTreeView->header()->hide();
    m_trackFinderTreeView->header()->setStretchLastSection(false);

    // CenterAreaWidget
    //        QWidget* mainWidget = new QWidget(this);
    //        m_mainLayout = new QGridLayout(this);
    //        mainWidget->setLayout(m_mainLayout);
    //        setCentralWidget(mainWidget);
    m_centerAreaWidget = new QStackedWidget(this);
    //        m_mainLayout->addWidget(m_centerAreaWidget, 0, 0);
    setCentralWidget(m_centerAreaWidget);

    // HistoryView
    m_historyDW = new QDockWidget(tr("History"), this);
    m_historyDW->setObjectName("HistoryDockWidget");
    m_historyWidget = new HistoryWidget(pm().get_undogroup(), m_historyDW);
    m_historyWidget->setFocusPolicy(Qt::NoFocus);
    m_historyDW->setWidget(m_historyWidget);
    addDockWidget(Qt::RightDockWidgetArea, m_historyDW);

    // AudioSources View
    m_audioSourcesDW = new QDockWidget(tr("Resources | Files"), this);
    m_audioSourcesDW->setObjectName("AudioSourcesDockWidget");
    m_audiosourcesview = new ResourcesWidget(m_audioSourcesDW);
    m_audiosourcesview->setFocusPolicy(Qt::NoFocus);
    m_audioSourcesDW->setWidget(m_audiosourcesview);
    addDockWidget(Qt::TopDockWidgetArea, m_audioSourcesDW);
    m_audioSourcesDW->hide();

    // Meter Widgets
    m_correlationMeterDW = new QDockWidget(tr("Correlation Meter"), this);
    m_correlationMeterDW->setObjectName("CorrelationMeterDockWidget");
    m_correlationMeter = new CorrelationMeterWidget(m_correlationMeterDW);
    m_correlationMeter->setFocusPolicy(Qt::NoFocus);
    m_correlationMeterDW->setWidget(m_correlationMeter);
    addDockWidget(Qt::TopDockWidgetArea, m_correlationMeterDW);
    m_correlationMeterDW->hide();

    m_spectralMeterDW = new QDockWidget(tr("FFT Spectrum"), this);
    m_spectralMeterDW->setObjectName("SpectralMeterDockWidget");
    m_spectralMeter = new SpectralMeterWidget(m_spectralMeterDW);
    m_spectralMeter->setFocusPolicy(Qt::NoFocus);
    m_spectralMeterDW->setWidget(m_spectralMeter);
    addDockWidget(Qt::TopDockWidgetArea, m_spectralMeterDW);
    m_spectralMeterDW->hide();

    // BusMonitor
    m_busMonitorDW = new QDockWidget(tr("VU Meters"), this);
    m_busMonitorDW->setObjectName(tr("VU Meters"));

    busMonitor = new TAudioBusVUMonitorWidget(m_busMonitorDW);
    m_busMonitorDW->setWidget(busMonitor);
    addDockWidget(Qt::RightDockWidgetArea, m_busMonitorDW);

    m_contextHelpDW = new QDockWidget(tr("Shortcuts Help"), this);
    m_contextHelpDW->setObjectName("ShortcutsHelpDockWidget");
    TContextHelpWidget* helpWidget = new TContextHelpWidget(m_contextHelpDW);
    helpWidget->setFocusPolicy(Qt::NoFocus);
    m_contextHelpDW->setWidget(helpWidget);
    addDockWidget(Qt::LeftDockWidgetArea, m_contextHelpDW);


    m_sysinfo = new SysInfoToolBar(this);
    m_sysinfo->setObjectName("System Info Toolbar");
    addToolBar(Qt::BottomToolBarArea, m_sysinfo);

    m_progressBar = new ProgressToolBar(this);
    m_progressBar->setObjectName("Progress Toolbar");
    addToolBar(Qt::BottomToolBarArea, m_progressBar);
    m_progressBar->hide();


    m_mainMenuToolBar = new QToolBar(this);
    m_mainMenuToolBar->setObjectName(tr("MainToolBar"));
    m_mainMenuToolBar->toggleViewAction()->setText("Main Tool Bar");
    //        m_mainMenuToolBar->setStyleSheet("margin-top: 0px; margin-bottom: 0px;");
    m_mainMenuToolBar->setMovable(false);

#if defined (Q_OS_MAC)
    // OS X is a lot pickier about menu bars. If we don't use
    // QMainWindow::menuBar(), the menus won't be shown at all.
    // If possible, I would recommend using the same solution
    // on other platforms as well, to reduce platform-specific
    // code. (ND)
    m_mainMenuBar = menuBar();
#else
    m_mainMenuBar = menuBar();
    // fix for appmenu-qt crash
    // Adding the main menu bar to a toolbar seems to be a
    // good idea after all, unity desktop crashes with it
    // so for now, disable this 'feature'
//    m_mainMenuToolBar->addWidget(m_mainMenuBar);
#endif
    addToolBar(Qt::TopToolBarArea, m_mainMenuToolBar);


    m_projectToolBar = new QToolBar(this);
    m_projectToolBar->setObjectName("Project Toolbar");
    m_projectToolBar->toggleViewAction()->setText(tr("Project Tool Bar"));
    addToolBar(Qt::TopToolBarArea, m_projectToolBar);

    m_editToolBar = new QToolBar(this);
    m_editToolBar->setObjectName("Edit Toolbar");
    m_editToolBar->toggleViewAction()->setText(tr("Edit Tool Bar"));
    addToolBar(Qt::TopToolBarArea, m_editToolBar);

    m_transportConsole = new TransportConsoleWidget(this);
    m_transportConsole->setObjectName("Transport Console");

#if defined (Q_OS_MAC)
    // this is important only when setUnifiedTitleAndToolBarOnMac() is true,
    // because in that case the toolbars in the TopToolBarArea can't be moved
    // and buttons outside the window area are not accessible at all.
    // And if set to true, it will mess up the session tab toolbar, too! (ND)

    bool unifiedTaTB = false;
    setUnifiedTitleAndToolBarOnMac(unifiedTaTB);
    if (unifiedTaTB) {
        addToolBar(Qt::BottomToolBarArea, m_transportConsole);
    } else {
        addToolBar(Qt::TopToolBarArea, m_transportConsole);
    }
    addToolBar(Qt::BottomToolBarArea, m_transportConsole);
#else
    addToolBar(Qt::TopToolBarArea, m_transportConsole);
#endif

    if (config().get_property("Themer", "textundericons", false).toBool()) {
        m_projectToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        m_editToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    }

    int iconsize = config().get_property("Themer", "iconsize", "22").toInt();
    m_projectToolBar->setIconSize(QSize(iconsize, iconsize));
    m_editToolBar->setIconSize(QSize(iconsize, iconsize));


    addToolBarBreak();

    m_sessionTabsToolbar = new QToolBar(this);
    m_sessionTabsToolbar->setObjectName("Sheet Tabs");
    m_sessionTabsToolbar->toggleViewAction()->setText("Sheet Tabs");
    addToolBar(Qt::TopToolBarArea, m_sessionTabsToolbar);
    if (m_sessionTabsToolbar->layout()) {
        m_sessionTabsToolbar->layout()->setSpacing(6);
    }

    m_welcomeWidget = new WelcomeWidget(this);
    m_welcomeWidget->show();
    m_centerAreaWidget->addWidget(m_welcomeWidget/*, tr("&0: Welcome")*/);
    m_welcomeWidget->setFocus(Qt::MouseFocusReason);

    // Some default values.
    m_project = nullptr;
    m_previousCenterAreaWidgetIndex = 0;
    m_currentSheetWidget = nullptr;
    m_exportDialog = nullptr;
    m_cdWritingDialog = nullptr;
    m_settingsdialog = nullptr;
    m_projectManagerDialog = nullptr;
    m_openProjectDialog = nullptr;
    m_newProjectDialog = nullptr;
    m_shortcutEditorDialog = nullptr;
    m_insertSilenceDialog = nullptr;
    m_newSheetDialog = nullptr;
    m_newTrackDialog = nullptr;
    m_quickStart = nullptr;
    m_restoreProjectBackupDialog = nullptr;
    m_vuLevelUpdateFrequency = 40;

    create_menus();

    /** Read in the Interface settings and apply them
	 */
    QSize mainScreenSize = config().get_property("Interface", "size", QSize(0, 0)).toSize();
    if (mainScreenSize.height()) {
        resize(mainScreenSize);
        move(config().get_property("Interface", "pos", QPoint(200, 200)).toPoint());
        restoreState(config().get_property("Interface", "windowstate", "").toByteArray());
    } else {
        showMaximized();
    }

    // Connections to core:
    connect(&pm(), &TProjectManager::projectLoaded, this, &TMainWindow::set_project);
    connect(&pm(), &TProjectManager::unsupportedProjectDirChangeDetected, this, &TMainWindow::project_dir_change_detected);
    connect(&pm(), &TProjectManager::projectLoadFailed, this, &TMainWindow::project_load_failed);
    connect(&pm(), &TProjectManager::projectFileVersionMismatch, this, &TMainWindow::project_file_mismatch, Qt::QueuedConnection);

    cpointer().add_contextitem(this);
    cpointer().add_contextitem(&tShortCutManager());
    ied().set_shortcut_manager(&tShortCutManager());

    connect(&config(), &TConfig::configChanged, this, &TMainWindow::config_changed);
    connect(&config(), &TConfig::configChanged, this, &TMainWindow::update_follow_state);

    connect(&tShortCutManager(), &TShortCutManager::functionKeysChanged, this, [this]() {
        for (auto menu : std::as_const(m_contextMenus)) {
            delete menu;
        }
        m_contextMenus.clear();
    });

    connect(themer(), &TThemer::themeLoaded, this, &TMainWindow::apply_theme);

    update_follow_state();

    //	setUnifiedTitleAndToolBarOnMac(true);

    m_vuLevelUpdateTimer.start(m_vuLevelUpdateFrequency, this);
    m_vuLevelPeakholdTimer.start(1000, this);
}

TMainWindow::~TMainWindow()
{
    PENTERDES;

    if (m_exportDialog) {
        delete m_exportDialog;
    }

    config().set_property("Interface", "size", size());
    config().set_property("Interface", "fullScreen", isFullScreen());
    config().set_property("Interface", "pos", pos());
    config().set_property("Interface", "windowstate", saveState());
}


void TMainWindow::set_project(TProject* project)
{
    PENTER;

    foreach(TSheetWidget* sw, m_sheetWidgets) {
        remove_session(sw->get_session());
    }

    m_project = project;

    m_trackFinderModel.clear();
    track_finder_show_initial_text();

    ctcache().invalidate_all();

    if ( m_project ) {
        connect(m_project, &TProject::projectLoadFinished, this, &TMainWindow::project_load_finished);
        connect(m_project, &TProject::projectLoadStarted, this, &TMainWindow::project_load_started);

        setWindowTitle(project->get_title() + " - Traverso");
        set_project_actions_enabled(true);

    } else {
        m_welcomeWidget->setFocus(Qt::MouseFocusReason);
        setWindowTitle("Traverso");
        set_project_actions_enabled(false);
        show_welcome_page();
    }
}

void TMainWindow::project_load_started()
{
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
}

void TMainWindow::project_load_finished()
{
    PENTER;

    QGuiApplication::restoreOverrideCursor();

    if (!m_project) {
        return;
    }

    connect(m_project, &TProject::currentSessionChanged, this, &TMainWindow::show_session);
    connect(m_project, &TProject::sheetAdded, this, &TMainWindow::add_sheetwidget);
    connect(m_project, &TProject::sheetRemoved, this, &TMainWindow::remove_sheetwidget);

    add_session(m_project);
    for(TSession* session : m_project->get_child_sessions()) {
        add_session(session);
    }

    for(TSheet* sheet : m_project->get_sheets()) {
        add_session(sheet);
        for(TSession* session : sheet->get_child_sessions()) {
            add_session(session);
        }
    }

    show_session(m_project->get_current_session());
}

void TMainWindow::remove_sheetwidget(TSheet* sheet)
{
    remove_session(sheet);
}

void TMainWindow::add_sheetwidget(TSheet* sheet)
{
    add_session(sheet);
}

void TMainWindow::add_session(TSession *session)
{
    if ( ! session->is_child_session()) {
        TSessionTabWidget* tabWidget = new TSessionTabWidget(m_sessionTabsToolbar, session);
        m_sessionTabsToolbar->addWidget(tabWidget);
        m_sessionTabWidgets.insert(session, tabWidget);
    }

    TSheetWidget* sheetWidget = new TSheetWidget(session, m_centerAreaWidget);
    m_sheetWidgets.insert(session, sheetWidget);
    m_centerAreaWidget->addWidget(sheetWidget);

    connect(session, &TSession::transportStopped, this, &TMainWindow::update_follow_state);
    connect(session, &TSession::tempFollowChanged, this, &TMainWindow::update_temp_follow_state);

    TSheet* sheet = qobject_cast<TSheet*>(session);
    if (sheet) {
        connect(sheet, &TSheet::snapChanged, this, &TMainWindow::update_snap_state);
        connect(session, &TSession::sessionAdded, this, &TMainWindow::add_session);
        connect(session, &TSession::sessionRemoved, this, &TMainWindow::remove_session);
    }
}

void TMainWindow::remove_session(TSession* session)
{
    TSheetWidget* sw = m_sheetWidgets.value(session);
    if (sw) {
        m_sheetWidgets.remove(session);
        m_centerAreaWidget->removeWidget(sw);
        if (m_currentSheetWidget == sw) {
            m_currentSheetWidget = nullptr;
        }
        delete sw;

        TSessionTabWidget* tabWidget = m_sessionTabWidgets.take(session);
        if (tabWidget) {
            delete tabWidget;
        }
    }
}


void TMainWindow::show_session(TSession* session)
{
    PENTER;
    // no reason to set same session again
    if (m_currentSheetWidget && m_currentSheetWidget->get_session() == session) {
        return;
    }

    TSheetWidget* sheetWidget = nullptr;

    if (!session) {
        m_snapAction->setEnabled(false);
        m_followAction->setEnabled(false);

        return;

    } else {
        sheetWidget = m_sheetWidgets.value(session);
        if (!sheetWidget) {
            return;
        }

        update_snap_state();
        m_snapAction->setEnabled(true);
        m_followAction->setEnabled(true);
    }

    if (m_currentSheetWidget && m_project && m_project->sheets_are_track_folder()) {
        sheetWidget->get_session()->set_hzoom(m_currentSheetWidget->get_session()->get_hzoom());
        sheetWidget->get_sheetview()->set_hscrollbar_value(m_currentSheetWidget->get_sheetview()->hscrollbar_value());
    }

    m_currentSheetWidget = sheetWidget;
    m_currentSheetWidget->setFocus(Qt::MouseFocusReason);

    m_centerAreaWidget->setCurrentWidget(m_currentSheetWidget);

    if (session) {
        TContextItem::get_undogroup()->setActiveStack(session->get_history_stack());
        // Update scrollbars in order to reset the snapList's range
        m_currentSheetWidget->get_sheetview()->update_scrollbars();
        //                setWindowTitle(m_project->get_title() + ": Sheet " + session->get_name() + " - Traverso");
    }
}

TCommand* TMainWindow::about_traverso()
{
    PENTER;
    QString text(tr("Traverso %1 (built with Qt %2)\n\n"
                    "A multitrack audio recording and editing program.\n\n"
                    "Look in the Help menu for more info.\n\n"
                    "Traverso is brought to you by R. Sijrier and others,\n"
                    "including all the people from the Free Software world\n"
                    "who contributed the important technologies on which\n"
                    "Traverso is based (Gcc, Qt, Xorg, Linux, and so on)" ).arg(VERSION, QT_VERSION_STR));
    QMessageBox::about ( this, tr("About Traverso"), text);

    return (TCommand*) 0;
}

TCommand* TMainWindow::quick_start()
{
    PENTER;

    if (!m_quickStart) {
        m_quickStart = new QDialog(this);
        Ui_QuickStartDialog *qsd = new Ui_QuickStartDialog();
        qsd->setupUi(m_quickStart);
    }
    m_quickStart->show();

    return (TCommand*) 0;
}

TCommand* TMainWindow::full_screen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
    return (TCommand*) 0;
}

TCommand* TMainWindow::show_fft_meter_only()
{
    if (m_centerAreaWidget->isHidden()) {
        m_centerAreaWidget->show();
        restoreState(m_windowState);
    } else {
        m_windowState = saveState();
        m_spectralMeterDW->show();

        m_busMonitorDW->hide();
        m_correlationMeterDW->hide();
        m_historyDW->hide();
        m_audioSourcesDW->hide();
        m_contextHelpDW->hide();
        m_projectToolBar->hide();
        m_editToolBar->hide();
        m_sessionTabsToolbar->hide();
        m_transportConsole->hide();
        m_centerAreaWidget->hide();
    }
    return 0;
}

void TMainWindow::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_vuLevelUpdateTimer.timerId()) {
        update_vu_levels_peak();
    }
    if (event->timerId() == m_vuLevelPeakholdTimer.timerId()) {
        reset_vu_levels_peak_hold_value();
    }
}

void TMainWindow::keyPressEvent( QKeyEvent * e)
{
    if (m_trackFinder->hasFocus()) {
        if (e->key() == Qt::Key_Escape) {
            track_finder_show_initial_text();
            if (m_currentSheetWidget) {
                m_currentSheetWidget->setFocus();
            }
        }
        return;
    }
    ied().catch_key_press(e);
    e->ignore();
}

void TMainWindow::keyReleaseEvent( QKeyEvent * e)
{
    ied().catch_key_release(e);
    e->ignore();
}

bool TMainWindow::eventFilter(QObject * obj, QEvent * event)
{
    if (event->type() == QEvent::MouseButtonPress && obj == m_trackFinder) {
        show_track_finder();
        return true;
    }

    QMenu* menu = qobject_cast<QMenu*>(obj);

    // If the installed filter was for a QMenu, we need to
    // delegate key releases to the InputEngine, e.g. a hold
    // action would never finish if we release the hold key
    // on the open Menu, resulting in weird behavior!
    if (menu) {
        if (event->type() == QEvent::KeyRelease) {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            ied().catch_key_release(keyEvent);
            return true;
        } else if (event->type() == QEvent::MouseMove) {
            // FIXME: Seems no longer to be the case??
            // Also send mouse move events to the current viewport
            // so in case we close the Menu, and _do_not_move_the_mouse
            // and perform an action, it could be delegated to the wrong ViewItem!
            // Obviously we don't want to send this event when the InputEngine is still
            // in holding mode, to avoid jog() being called for the active HoldCommand!
            //			QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            //                        ViewPort* vp = static_cast<ViewPort*>(cpointer().get_viewport());
            //			if (vp && !ie().is_holding()) {
            //				vp->mouseMoveEvent(mouseEvent);
            //			}
        } else {
            return false;
        }
    }

    return false;
}


void TMainWindow::changeEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::ActivationChange:
    case QEvent::WindowStateChange:
        // clean up the ie after Alt-Tab
        // if problems remain, maybe ie().reset() will help...
        ied().reject_current_hold_actions();
        break;
    default:
        break;
    }

    // pass the event on to the parent class
    QMainWindow::changeEvent(event);
}

TCommand * TMainWindow::show_export_widget( )
{
    if (m_cdWritingDialog && !m_cdWritingDialog->isHidden()) {
        return 0;
    }

    if (! m_exportDialog) {
        m_exportDialog = new ExportDialog(this);
    }

    if (m_exportDialog->isHidden()) {
        m_exportDialog->show();
    }

    return (TCommand*) 0;
}

TCommand * TMainWindow::show_cd_writing_dialog( )
{
    if (m_exportDialog && !m_exportDialog->isHidden()) {
        return 0;
    }

    if (! m_cdWritingDialog) {
        m_cdWritingDialog = new CDWritingDialog(this);
    }

    if (m_cdWritingDialog->isHidden()) {
        m_cdWritingDialog->show();
    }

    return (TCommand*) 0;
}

void TMainWindow::create_menus()
{
    QAction* action;

    // ==========================================
    // BESTAND (FILE) MENU
    // ==========================================
    QMenu* fileMenu = m_mainMenuBar->addMenu(tr("&File"));
    fileMenu->installEventFilter(this);

    // Nieuw Project
    action = fileMenu->addAction(tr("&New..."));
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentNew, QIcon(QStringLiteral(":/icons/new.svg"))));
    action->setShortcut(QKeySequence::New);
    connect(action, &QAction::triggered, this, &TMainWindow::show_newproject_dialog);

    // Open Project / Welcome Page
    action = fileMenu->addAction(tr("&Open..."));
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentOpen, QIcon(QStringLiteral(":/icons/open.svg"))));
    action->setShortcut(QKeySequence::Open);
    connect(action, &QAction::triggered, this, &TMainWindow::show_welcome_page);

    fileMenu->addSeparator();

    // Project Opslaan
    action = fileMenu->addAction(tr("&Save"));
    m_projectMenuToolbarActions.append(action);
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentSave, QIcon(QStringLiteral(":/icons/save.svg"))));
    action->setShortcut(QKeySequence::Save);
    connect(action, &QAction::triggered, &pm(), &TProjectManager::save_project);

    fileMenu->addSeparator();

    // Project Sluiten
    action = fileMenu->addAction(tr("&Close Project"));
    m_projectMenuToolbarActions.append(action);
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::WindowClose, QIcon(QStringLiteral(":/icons/close.svg"))));
    action->setShortcut(QKeySequence::Close);
    connect(action, &QAction::triggered, &pm(), &TProjectManager::close_current_project);

    fileMenu->addSeparator();

    // Project Beheer (F4)
    action = fileMenu->addAction(tr("&Manage Project..."));
    m_projectMenuToolbarActions.append(action);
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentProperties, QIcon(QStringLiteral(":/icons/projectmanager.svg"))));
    action->setShortcut(QKeySequence(QStringLiteral("F4")));
    fileMenu->addAction(action);
    m_projectToolBar->addAction(action);
    connect(action, &QAction::triggered, this, &TMainWindow::show_project_manager_dialog);

    // Audio Exporteren (F9)
    action = fileMenu->addAction(tr("&Export..."));
    m_projectMenuToolbarActions.append(action);
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DocumentSend, QIcon(QStringLiteral(":/icons/export.svg"))));
    action->setShortcut(QKeySequence(QStringLiteral("F9")));
    m_projectToolBar->addAction(action);
    connect(action, &QAction::triggered, this, &TMainWindow::show_export_widget);

    // CD Branden (F8)
    action = fileMenu->addAction(tr("&CD Writing..."));
    m_projectMenuToolbarActions.append(action);
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::DriveOptical, QIcon(QStringLiteral(":/icons/write-cd.svg"))));
    action->setShortcut(QKeySequence(QStringLiteral("F8")));
    m_projectToolBar->addAction(action);
    connect(action, &QAction::triggered, this, &TMainWindow::show_cd_writing_dialog);

    // Back-up Herstellen (F10)
    action = fileMenu->addAction(tr("&Restore Backup..."));
    m_projectMenuToolbarActions.append(action);
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::EditUndo, QIcon(QStringLiteral(":/icons/restore.svg"))));
    action->setShortcut(QKeySequence(QStringLiteral("F10")));
    fileMenu->addAction(action);
    m_projectToolBar->addAction(action);
    connect(action, &QAction::triggered, this, QOverload<>::of(&TMainWindow::show_restore_project_backup_dialog));

    fileMenu->addSeparator();

    // Applicatie Afsluiten
    action = fileMenu->addAction(tr("&Quit"));
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ApplicationExit, QIcon(QStringLiteral(":/icons/exit.svg"))));
    action->setShortcut(QKeySequence(QStringLiteral("CTRL+Q")));
    connect(action, &QAction::triggered, &pm(), &TProjectManager::exit);


    // ==========================================
    // BEWERKEN (EDIT) MENU
    // ==========================================
    QMenu* editMenu = m_mainMenuBar->addMenu(tr("&Edit"));
    editMenu->installEventFilter(this);

    // Undo
    action = editMenu->addAction(tr("Undo"));
    m_projectMenuToolbarActions.append(action);
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::EditUndo, QIcon(QStringLiteral(":/icons/undo.svg"))));
    action->setShortcut(QKeySequence::Undo);
    m_editToolBar->addAction(action);
    connect(action, &QAction::triggered, this, &TMainWindow::undo);

    // Redo
    action = editMenu->addAction(tr("Redo"));
    m_projectMenuToolbarActions.append(action);
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::EditRedo, QIcon(QStringLiteral(":/icons/redo.svg"))));
    action->setShortcut(QKeySequence::Redo);
    m_editToolBar->addAction(action);
    connect(action, &QAction::triggered, this, &TMainWindow::redo);

    editMenu->addSeparator();
    m_editToolBar->addSeparator();

    // Audio Importeren
    action = editMenu->addAction(tr("Import &Audio..."));
    m_projectMenuToolbarActions.append(action);
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::ListAdd, QIcon(QStringLiteral(":/icons/import-audio.svg"))));
    m_editToolBar->addAction(action);
    connect(action, &QAction::triggered, this, &TMainWindow::import_audio);

    // Stilte Invoegen
    action = editMenu->addAction(tr("Insert Si&lence..."));
    m_projectMenuToolbarActions.append(action);
    action->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::InsertLink, QIcon(QStringLiteral(":/icons/import-silence.svg"))));
    m_editToolBar->addAction(action);
    connect(action, &QAction::triggered, this, [this](bool checked){ (void)checked; this->show_insertsilence_dialog(); });

    editMenu->addSeparator();
    m_editToolBar->addSeparator();

    // Magnetische Snap (Vaste functionele kleur behouden via setIsMask(false))
    m_snapAction = editMenu->addAction(tr("&Snap"));
    m_projectMenuToolbarActions.append(m_snapAction);
    QIcon snapIcon = QIcon::fromTheme(QStringLiteral("snap-magnet"), QIcon(QStringLiteral(":/icons/snap-magnet.svg")));
    snapIcon.setIsMask(false);
    m_snapAction->setIcon(snapIcon);
    m_snapAction->setCheckable(true);
    m_snapAction->setToolTip(tr("Snap items to edges of other items while dragging."));
    m_editToolBar->addAction(m_snapAction);
    connect(m_snapAction, &QAction::triggered, this, &TMainWindow::snap_state_changed);

    // Scroll Playback (Follow Head)
    m_followAction = editMenu->addAction(tr("S&croll Playback"));
    m_projectMenuToolbarActions.append(m_followAction);
    m_followAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::GoNext, QIcon(QStringLiteral(":/icons/follow.svg"))));
    m_followAction->setCheckable(true);
    m_followAction->setToolTip(tr("Keep play cursor in view while playing or recording."));
    m_editToolBar->addAction(m_followAction);
    connect(m_followAction, &QAction::triggered, this, &TMainWindow::follow_state_changed);


    // ==========================================
    // BEELD (VIEW) MENU
    // ==========================================
    QMenu* viewMenu = m_mainMenuBar->addMenu(tr("&View"));
    viewMenu->installEventFilter(this);

    viewMenu->addAction(m_historyDW->toggleViewAction());
    viewMenu->addAction(m_busMonitorDW->toggleViewAction());
    viewMenu->addAction(m_audioSourcesDW->toggleViewAction());
    viewMenu->addAction(m_contextHelpDW->toggleViewAction());

    // Marker Editor
    action = viewMenu->addAction(tr("Marker Editor..."));
    m_projectMenuToolbarActions.append(action);
    connect(action, &QAction::triggered, this, &TMainWindow::show_marker_dialog);

    // Volledig Scherm
    action = viewMenu->addAction(tr("Toggle Full Screen"));
    connect(action, &QAction::triggered, this, &TMainWindow::full_screen);

    // FFT Analyse Modus
    action = viewMenu->addAction(tr("Toggle FFT Only"));
    connect(action, &QAction::triggered, this, &TMainWindow::show_fft_meter_only);

    viewMenu->addSeparator();

    viewMenu->addAction(m_correlationMeterDW->toggleViewAction());
    viewMenu->addAction(m_spectralMeterDW->toggleViewAction());

    viewMenu->addSeparator();
    action = viewMenu->addAction(tr("ToolBars"));
    action->setEnabled(false);
    viewMenu->addSeparator();

    // Toolbars in/uitschakelen
    viewMenu->addAction(m_transportConsole->toggleViewAction());
    m_transportConsole->toggleViewAction()->setText(tr("Transport Console"));

#if !defined (Q_OS_MAC)
    viewMenu->addAction(m_projectToolBar->toggleViewAction());
    m_projectToolBar->toggleViewAction()->setText(tr("Project"));

    viewMenu->addAction(m_editToolBar->toggleViewAction());
    m_editToolBar->toggleViewAction()->setText(tr("Edit"));
#endif

    viewMenu->addAction(m_mainMenuToolBar->toggleViewAction());
    viewMenu->addAction(m_sessionTabsToolbar->toggleViewAction());
    viewMenu->addAction(m_sysinfo->toggleViewAction());
    m_sysinfo->toggleViewAction()->setText(tr("System Information"));

    viewMenu->addSeparator();


    // ==========================================
    // INSTELLINGEN (SETTINGS) MENU
    // ==========================================
    QMenu* settingsMenu = m_mainMenuBar->addMenu(tr("&Settings"));
    settingsMenu->installEventFilter(this);

    // Submenu: Opname Formaat
    m_encodingMenu = settingsMenu->addMenu(tr("&Recording File Format"));

    action = m_encodingMenu->addAction(QStringLiteral("WAVE"));
    action->setData(QStringLiteral("wav"));
    connect(action, &QAction::triggered, this, &TMainWindow::change_recording_format_to_wav);

    action = m_encodingMenu->addAction(QStringLiteral("WavPack"));
    action->setData(QStringLiteral("wavpack"));
    connect(action, &QAction::triggered, this, &TMainWindow::change_recording_format_to_wavpack);

    action = m_encodingMenu->addAction(QStringLiteral("WAVE-64"));
    action->setData(QStringLiteral("w64"));
    connect(action, &QAction::triggered, this, &TMainWindow::change_recording_format_to_wav64);
    // Submenu: Resample Kwaliteit
    m_resampleQualityMenu = settingsMenu->addMenu(tr("Resample &Quality"));
    m_resampleQualityMenu->setToolTipsVisible(true);
    for (int convertorType : TResampleAudioReader::get_convertor_types()) {
        action = m_resampleQualityMenu->addAction(TResampleAudioReader::get_convertor_type_name(convertorType));
        action->setData(convertorType);
        action->setToolTip(TResampleAudioReader::get_convertor_type_description(convertorType));
        connect(action, &QAction::triggered, this, [this, action, convertorType] {
            config().set_property("Conversion", "RTResamplingConverterType", convertorType);
            save_config_and_emit_message(tr("Changed resample quality to: %1").arg(action->text()));
        });
    }
    config_changed();
    settingsMenu->addSeparator();
    // Sneltoetsen Configuratiescherm
    action = settingsMenu->addAction(tr("&Shortcut Configuration"));
    connect(action, &QAction::triggered, this, &TMainWindow::show_shortcuts_edit_dialog);
    // Voorkeursinstellingen
    action = settingsMenu->addAction(tr("&Preferences..."));
    connect(action, &QAction::triggered, this, &TMainWindow::show_settings_dialog);
    // ==========================================
    // HULP (HELP) MENU
    // ==========================================
    QMenu* helpMenu = m_mainMenuBar->addMenu(tr("&Help"));
    helpMenu->installEventFilter(this);
    // Snel aan de slag
    action = helpMenu->addAction(tr("&Getting Started"));
    connect(action, &QAction::triggered, this, &TMainWindow::quick_start);
    // Gebruikershandleiding
    action = helpMenu->addAction(tr("&User Manual"));
    action->setIcon(style()->standardIcon(QStyle::SP_DialogHelpButton));
    connect(action, &QAction::triggered, this, &TMainWindow::open_help_browser);
    // Over Traverso
    action = helpMenu->addAction(tr("&About Traverso"));
    connect(action, &QAction::triggered, this, &TMainWindow::about_traverso);
    set_project_actions_enabled(false);
}


void TMainWindow::set_project_actions_enabled(bool enable)
{
    foreach(QAction* action, m_projectMenuToolbarActions) {
        action->setEnabled(enable);
    }

}


DISPATCH_RULE_IS_ALWAYS TCommand * TMainWindow::show_context_menu( )
{
    QList<QObject* > items;

    // In case of a holding action, show the menu for the holding command!
    // If not, show the menu for the topmost context item, and it's
    // siblings as submenus
    if (ied().is_holding()) {
        TCommand* holding = ied().get_holding_command();
        if (holding) {
            items.append(holding);
        }
    } else {
        items = cpointer().get_context_items();

        // Filter out classes that don't need to show up in the menu
        foreach(QObject* item, items) {
            QString className = item->metaObject()->className();
            if ( ( ! className.contains("View")) || className.contains("ViewPort") ) {
                items.removeAll(item);
            }
        }
    }

    if (items.isEmpty()) {
        printf("Interface:: No items under mouse to show context menu for!\n");
        return nullptr;
    }

    // 'Store' the contextitems under the mouse cursor, so the InputEngine
    // dispatches the 'keyfact' from the menu to the 'pointed' objects!
    cpointer().set_contextmenu_items(cpointer().get_context_items());

    QMenu* toplevelmenu = nullptr;
    QAction* action = nullptr;

    if (items.size()) {
        QObject* item = items.first();
        QString className = item->metaObject()->className();

        toplevelmenu = m_contextMenus.value(className);

        if ( ! toplevelmenu ) {
            while (items.size() > 0) {
                toplevelmenu = create_context_menu(item);
                if (! toplevelmenu ) {
                    items.removeFirst();
                    item = items.first();
                    className = item->metaObject()->className();
                } else {
                    break;
                }
            }

            if (!toplevelmenu) {
                return nullptr;
            }

            if (items.size() > 1 ) {
                // Create submenus
                toplevelmenu->addSeparator();

                for (int i=items.size() -1; i>0; --i) {
                    {
                        QObject* item = items.at(i);
                        QString className = item->metaObject()->className();
                        QMenu* menu = create_context_menu(item);
                        if (! menu) {
                            continue;
                        }
                        action = toplevelmenu->insertMenu(action, menu);
                        QString name = tShortCutManager().get_translation_for(className);

                        action->setText(name);
                    }
                }
            }

            m_contextMenus.insert(className, toplevelmenu);
            connect(toplevelmenu, &QMenu::triggered, this, &TMainWindow::context_menu_action_triggered);
        }
    }

    // It's impossible there is NO toplevelmenu, but oh well...
    if (toplevelmenu) {
        // using toplevelmenu->exec() continues on the event loop
        // when the hold action finishes, input engine clears hold items
        // which could be referenced again in inputengine, causing a segfault.
        // so showing it will be sufficient. In fact, using exec() is
        // considered bad practice due this very issue.
        toplevelmenu->popup(QCursor::pos());

        if (ied().is_holding()) {
            //            QGuiApplication::restoreOverrideCursor();
        }
    }

    return nullptr;
}

QMenu* TMainWindow::create_context_menu(QObject* item, QList<TShortCutFunction* >* menulist)
{
    QList<TShortCutFunction* > list;
    if (item) {
        list = tShortCutManager().get_shortcut_functions_for_class(item->metaObject()->className());
    } else {
        list = *menulist;
    }

    if (list.size() == 0) {
        // Empty menu!
        return nullptr;
    }

    QString name;
    if (item) {
        name = tShortCutManager().get_translation_for(QString(item->metaObject()->className()));
    }

    QMenu* menu = new QMenu(this);
    menu->installEventFilter(this);

    QAction* menuAction = menu->addAction(name);
    QFont font(themer()->get_font("ContextMenu:fontscale:actions"));
    font.setBold(true);
    menuAction->setFont(font);
    menuAction->setEnabled(false);
    menu->addSeparator();
    menu->setFont(themer()->get_font("ContextMenu:fontscale:actions"));

    QMultiMap<QString, TShortCutFunction* > submenus;

    for (int i=0; i<list.size(); ++i) {
        TShortCutFunction* function = list.at(i);

        // If this MenuData item is a submenu, add to the
        // list of submenus, which will be processed lateron
        // Else, add the MenuData item as action in the Menu
        if (function->get_submenu_name().isEmpty()) {
            add_function_to_menu(function, menu);
        } else {
            submenus.insert(function->get_submenu_name(), function);
        }
    }

    // For all submenus, create the Menu, and add
    // actions, a little code duplication here, adding action to the
    // menu is also done ~10 lines up ...
    for(const QString &key : submenus.uniqueKeys()) {
        QList<TShortCutFunction*> list = submenus.values(key);

        std::sort(list.begin(), list.end(), [&](TShortCutFunction* left, TShortCutFunction* right) {
            return left->get_sort_order() < right->get_sort_order();
        });

        QMenu* subMenu = new QMenu(this);
        subMenu->setFont(themer()->get_font("ContextMenu:fontscale:actions"));

        QFont font(themer()->get_font("ContextMenu:fontscale:actions"));
        font.setBold(true);
        subMenu->menuAction()->setFont(font);

        QAction* action = menu->insertMenu(nullptr, subMenu);
        action->setText(tShortCutManager().get_translation_for(key));
        for(TShortCutFunction* function : list) {
            add_function_to_menu(function, subMenu);
        }
    }

    return menu;
}

#include <QPainter>
#include <QApplication>
#include <QPalette>

QPixmap TMainWindow::find_pixmap(const QString &pixname)
{
    QPixmap pixmap;

    if (!QPixmapCache::find(pixname, &pixmap))
    {
        QIcon icon = QIcon(QStringLiteral(":/ui/") + pixname);
        QPixmap rawPixmap = icon.pixmap(QSize(64, 64));

        if (!rawPixmap.isNull()) {
            // 2. Prepare a clean target canvas with support for transparency (Alpha channel)
            pixmap = QPixmap(rawPixmap.size());
            pixmap.fill(Qt::transparent);

            // 3. Extract the real dynamic theme configuration text color from the OS / Application layer
            QColor themeColor = qApp->palette().color(QPalette::WindowText);

            // 4. Use a context painter to tint the black SVG layers to match your exact dark/light theme
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.drawPixmap(0, 0, rawPixmap);

            // Overwrite the color channels while preserving the exact vector antialiasing/transparency
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), themeColor);
            painter.end();

            // 5. Store the high-DPI themed canvas slice into the centralized layout runtime cache
            QPixmapCache::insert(pixname, pixmap);
        } else {
            qWarning() << "TMainWindow::find_pixmap: Vector rendering failed for context key:" << pixname;
        }
    }

    return pixmap;
}


void TMainWindow::add_function_to_menu(TShortCutFunction *function, QMenu *menu)
{
    QAction* action = menu->addAction(function->get_description());
    QKeySequence sequence(function->get_key_sequence().remove(" "));
    action->setShortcut(sequence);
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    action->setShortcutVisibleInContextMenu(true);
#endif
    QVariant v = QVariant::fromValue(static_cast<void*>(function));
    action->setData(v);
}

void TMainWindow::context_menu_action_triggered(QAction *action)
{
    ied().dispatch_shortcut_from_contextmenu((TShortCutFunction*)action->data().value<void*>());
}

void TMainWindow::set_insertsilence_track(TAudioTrack* track)
{
    if (m_insertSilenceDialog) {
        m_insertSilenceDialog->setTrack(track);
    }
}

void TMainWindow::select_fade_in_shape( )
{
    QMenu* menu = m_contextMenus.value("fadeInSelector");

    if (!menu) {
        menu = create_fade_selector_menu("fadeInSelector");
        connect(menu, &QMenu::triggered, this, &TMainWindow::set_fade_in_shape);
    }

    menu->popup(QCursor::pos());
}

void TMainWindow::select_fade_out_shape( )
{
    QMenu* menu = m_contextMenus.value("fadeOutSelector");

    if (!menu) {
        menu = create_fade_selector_menu("fadeOutSelector");
        connect(menu, &QMenu::triggered, this, &TMainWindow::set_fade_out_shape);
    }

    menu->popup(QCursor::pos());
}


void TMainWindow::set_fade_in_shape( QAction * action )
{
    QList<QObject* > items = cpointer().get_context_items();
    foreach(QObject* obj, items) {
        TAudioClipView* acv = qobject_cast<TAudioClipView*>(obj);
        if (acv) {
            acv->get_clip()->get_fade_in()->set_shape(action->data().toString());
            break;
        }
    }
}

void TMainWindow::set_fade_out_shape( QAction * action )
{
    QList<QObject* > items = cpointer().get_context_items();
    foreach(QObject* obj, items) {
        TAudioClipView* acv = qobject_cast<TAudioClipView*>(obj);
        if (acv) {
            acv->get_clip()->get_fade_out()->set_shape(action->data().toString());
            break;
        }
    }
}


QMenu* TMainWindow::create_fade_selector_menu(const QString& fadeTypeName)
{
    QMenu* menu = new QMenu(this);

    foreach(QString name, TFadeCurve::defaultShapes) {
        QAction* action = menu->addAction(name);
        action->setData(name);
    }

    m_contextMenus.insert(fadeTypeName, menu);

    return menu;
}

// 1. CLEANED UP: Only handles pure application and audio device parameters
void TMainWindow::config_changed()
{
    QString encoding = config().get_property("Recording", "FileFormat", "wav").toString();
    QList<QAction* > actions = m_encodingMenu->actions();

    foreach(QAction* action, actions) {
        if (action->data().toString() == encoding) {
            action->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
        } else {
            action->setIcon(QIcon());
        }
    }

    int quality = config().get_property("Conversion", "RTResamplingConverterType", TResampleAudioReader::get_default_resample_quality()).toInt();
    actions = m_resampleQualityMenu->actions();

    bool useResampling = config().get_property("Conversion", "DynamicResampling", true).toBool();
    if (useResampling) {
        m_resampleQualityMenu->setEnabled(true);
    } else {
        m_resampleQualityMenu->setEnabled(false);
    }

    foreach(QAction* action, actions) {
        if (action->data().toInt() == quality) {
            action->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
        } else {
            action->setIcon(QIcon());
        }
    }
}

void TMainWindow::apply_theme()
{
    // Apply toolbar button layout styles from configuration
    switch (config().get_property("Themer", "toolbuttonstyle", 0).toInt()) {
    case 0:
        m_projectToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_editToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        break;
    case 1:
        m_projectToolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_editToolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
        break;
    case 2:
        m_projectToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_editToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        break;
    case 3:
        m_projectToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        m_editToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        break;
    }

    int iconsize = config().get_property("Themer", "iconsize", "22").toInt();
    m_projectToolBar->setIconSize(QSize(iconsize, iconsize));
    m_editToolBar->setIconSize(QSize(iconsize, iconsize));

    int transportconsolesize = config().get_property("Themer", "transportconsolesize", "22").toInt();
    m_transportConsole->setIconSize(QSize(transportconsolesize, transportconsolesize));
    m_transportConsole->resize(m_transportConsole->sizeHint());

    // // Pull active vector colors from core data layer
    // QColor primary   = themer()->get_color("Toolbar:icon-primary");
    // QColor secondary = themer()->get_color("Toolbar:icon-secondary");

    // // Push coordinates to the target layouts
    // QPalette editToolbarPalette = m_editToolBar->palette();
    // editToolbarPalette.setColor(QPalette::WindowText, primary);
    // editToolbarPalette.setColor(QPalette::Link, secondary);
    // m_editToolBar->setPalette(editToolbarPalette);

    // QPalette projectToolbarPalette = m_projectToolBar->palette();
    // projectToolbarPalette.setColor(QPalette::WindowText, primary);
    // projectToolbarPalette.setColor(QPalette::Link, secondary);
    // m_projectToolBar->setPalette(projectToolbarPalette);

    m_snapAction->setIcon(QIcon::fromTheme("snap-magnet"));

    foreach (QAction* act, m_editToolBar->actions()) {
        if (act->text().contains("Undo")) {
            act->setIcon(QIcon());
            act->setIcon(QIcon::fromTheme("edit-undo"));
        }
        if (act->text().contains("Redo")) {
            act->setIcon(QIcon());
            act->setIcon(QIcon::fromTheme("edit-redo"));
        }
        if (act->text().contains("Snap")) {
            act->setIcon(QIcon::fromTheme("snap-magnet"));
        }
    }

    foreach (QAction* act, m_transportConsole->actions()) {
        if (act->text().contains("Play")) act->setIcon(QIcon::fromTheme("media-playback-start"));
        if (act->text().contains("Stop")) act->setIcon(QIcon::fromTheme("media-playback-stop"));
        if (act->text().contains("Record")) act->setIcon(QIcon::fromTheme("media-record"));
    }
}

void TMainWindow::import_audio()
{
    TProject* project = pm().get_project();
    if (!project) {
        return;
    }

    TSheet* sheet = m_currentSheetWidget->get_sheet();
    if (!sheet || !sheet->get_audio_track_count()) {
        return;
    }

    QStringList files = QFileDialog::getOpenFileNames(this, tr("Open Audio Files"),
                                                      project->get_import_dir(),
                                                      tr("Audio files (*.wav *.flac *.ogg *.mp3 *.wv *.w64 *.m4a *.aac)"));

    if (files.isEmpty()) {
        return;
    }

    QList<TAudioTrack*> tracks = sheet->get_audio_tracks();
    TAudioTrack*	track = tracks.first();

    ImportClipsDialog *importClips = new ImportClipsDialog(this);

    importClips->set_tracks(tracks);

    if (importClips->exec() == QDialog::Accepted) {
        if (!importClips->has_tracks()) {
            delete importClips;
            return;
        }

        track = importClips->get_selected_track();
    }

    // append the clips to the selected track
    TTimeRef importLocation = track->get_end_location();

    TTimeLineRuler* timeLineRuler = sheet->get_timeline_ruler();
    int n = timeLineRuler->get_markers().size() + 1;
    if (timeLineRuler->has_end_marker()) {
        n -= 1;
    }

    QList<TAudioClip*> clips;
    QList<TTimeLineMarker*> markers;

    while(!files.isEmpty()) {
        QString fileName = files.takeFirst();
        TAudioClip* clip = TAudioClipFactory::create_from_file(track, fileName, importLocation);

        if (clip) {
            QFileInfo fi(fileName);
            TTimeLineMarker* m = new TTimeLineMarker(timeLineRuler, importLocation);
            m->set_description(QString(tr("%1: %2")).arg(n).arg(fi.baseName()));

            clips.append(clip);
            markers.append(m);
            importLocation += clip->get_length();
        }
        ++n;
    }

    delete importClips;

    if (clips.isEmpty()) {
        return;
    }

    // The clips and markers are added as one undo step
    QUndoStack* undoStack = sheet->get_history_stack();
    if (undoStack) {
        undoStack->beginMacro(tr("Import Audio Files"));
    }

    QUndoCommand* command = sheet->create_command_mutate_clips(clips, {}, tr("Import Audio Files"));
    if (undoStack) {
        undoStack->push(command);
    } else {
        command->redo();
        delete command;
    }

    for (TTimeLineMarker* m : markers) {
        TCommand::process_command(timeLineRuler->add_marker(m, true));
    }

    if (timeLineRuler->has_end_marker()) {
        TTimeLineMarker* m = timeLineRuler->get_end_marker();
        TTimeRef origLocation = m->get_location()->get_start();
        QUndoCommand* moveEndMarker = new TUndoCommand(tr("Move End Marker"),
            [m, importLocation]() { m->set_when(importLocation); },
            [m, origLocation]() { m->set_when(origLocation); });
        if (undoStack) {
            undoStack->push(moveEndMarker);
        } else {
            moveEndMarker->redo();
            delete moveEndMarker;
        }
    } else {
        TTimeLineMarker* m = new TTimeLineMarker(timeLineRuler, importLocation, TTimeLineMarker::ENDMARKER);
        TCommand::process_command(timeLineRuler->add_marker(m, true));
    }

    if (undoStack) {
        undoStack->endMacro();
    }
}


void TMainWindow::show_settings_dialog()
{
    if (!m_settingsdialog) {
        m_settingsdialog = new SettingsDialog(this);
    }

    m_settingsdialog->show();
}

void TMainWindow::show_settings_dialog_sound_system_page()
{
    show_settings_dialog();
    m_settingsdialog->show_page("Sound System");
}


void TMainWindow::closeEvent(QCloseEvent * event)
{
    event->ignore();
    pm().exit();
}

TCommand* TMainWindow::show_project_manager_dialog()
{
    if (! m_projectManagerDialog) {
        m_projectManagerDialog = new ProjectManagerDialog(this);
    }
    m_projectManagerDialog->show();
    return 0;
}

TCommand* TMainWindow::show_open_project_dialog()
{
    if (!m_openProjectDialog) {
        m_openProjectDialog = new OpenProjectDialog(this);
    }
    m_openProjectDialog->show();
    return 0;
}

TCommand * TMainWindow::show_newproject_dialog()
{
    if (! m_newProjectDialog ) {
        m_newProjectDialog = new NewProjectDialog(this);
        TAudioFileCopyConvert* converter = m_newProjectDialog->get_converter();
        connect(converter, &TAudioFileCopyConvert::taskStarted, m_progressBar, &ProgressToolBar::set_label);
        connect(converter, &TAudioFileCopyConvert::progress, m_progressBar, &ProgressToolBar::set_progress);
        connect(m_newProjectDialog, &NewProjectDialog::numberOfFiles, m_progressBar, &ProgressToolBar::set_num_files);
    }
    m_newProjectDialog->show();
    return 0;
}

TCommand * TMainWindow::show_insertsilence_dialog(TAudioTrack *track)
{
    if (! m_insertSilenceDialog) {
        m_insertSilenceDialog = new InsertSilenceDialog(this);
    }

    m_insertSilenceDialog->setTrack(track);
    m_insertSilenceDialog->focusInput();
    m_insertSilenceDialog->show();

    return ied().succes();
}


TCommand * TMainWindow::show_marker_dialog()
{
    MarkerDialog markerDialog(this);

    markerDialog.exec();

    return nullptr;
}

TCommand* TMainWindow::show_add_child_session_dialog()
{
    if (!m_project) {
        return 0;
    }

    TSheet* activeSheet = m_project->get_active_sheet();
    TSession* activeSession = m_project->get_current_session();
    TSession* parentSession = 0;

    if (activeSession->is_project_session()) {
        parentSession = activeSession;
    } else if (activeSheet) {
        parentSession = activeSheet;
    } else {
        tInformUser().information(tr("No Sheet active to add child view to"));
        return 0;
    }

    TSession* session = new TSession(parentSession);

    TTrackSelector selector(this, parentSession, session);
    if (selector.exec() == QDialog::Accepted) {
        parentSession->add_child_session(session);
        m_project->set_current_session(session->get_id());
    } else {
        delete session;
    }

    return 0;
}


QSize TMainWindow::sizeHint() const
{
    return QSize(800, 600);
}

TCommand* TMainWindow::show_newsheet_dialog()
{
    if (! m_newSheetDialog) {
        m_newSheetDialog = new NewSheetDialog(this);
    }

    m_newSheetDialog->show();

    return 0;
}

TCommand* TMainWindow::show_newtrack_dialog()
{
    if (!m_project) {
        return 0;
    }

    TSession* activeSession = m_project->get_current_session();
    TSheet* sheet = qobject_cast<TSheet*>(activeSession);
    TProject* project = qobject_cast<TProject*>(activeSession);

    if (sheet || project) {
        if (! m_newTrackDialog) {
            m_newTrackDialog = new NewTrackDialog(this);
        }

        m_newTrackDialog->show();
    } else {
        if (!activeSession) {
            return 0;
        }

        if (activeSession->get_parent_session()) {
            TTrackSelector selector(this, activeSession->get_parent_session(), activeSession);
            selector.exec();
            return 0;
        }
    }


    return 0;
}

TCommand* TMainWindow::show_shortcuts_edit_dialog()
{
    if (!m_shortcutEditorDialog)
    {
        m_shortcutEditorDialog = new TShortcutEditorDialog(this);
    }
    m_shortcutEditorDialog->show();
    return 0;
}

void TMainWindow::open_help_browser()
{
    tInformUser().information(tr("Opening User Manual in external browser!"));
    QDesktopServices::openUrl(QUrl("http://traverso-daw.org/UserManual"));
}

void TMainWindow::project_dir_change_detected()
{
    QMessageBox::critical(this, tr("Traverso - Important"),
                          tr("A Project directory changed outside of Traverso. \n\n"
                             "This is NOT supported! Please undo this change now!\n\n"
                             "If you want to rename a Project, use the Project Manager instead!"),
                          QMessageBox::Ok);
}

TCommand * TMainWindow::show_restore_project_backup_dialog(QString projectname)
{
    if (! m_restoreProjectBackupDialog) {
        m_restoreProjectBackupDialog = new RestoreProjectBackupDialog(this);
    }

    m_restoreProjectBackupDialog->set_project_name(projectname);
    m_restoreProjectBackupDialog->show();


    return 0;
}

void TMainWindow::show_restore_project_backup_dialog()
{
    TProject* project = pm().get_project();

    if (! project ) {
        return;
    }

    show_restore_project_backup_dialog(project->get_title());
}

void TMainWindow::project_load_failed(QString project, QString reason)
{
    QMessageBox::critical(	this, tr("Traverso - Project load failed"),
                          tr("The requested Project `%1` \ncould not be loaded for the following reason:\n\n'%2'"
                             "\n\nYou will now be given a list of available backups (if any) \n"
                             "to restore the Project from.").arg(project).arg(reason),
                          QMessageBox::Ok);

    show_restore_project_backup_dialog(project);
}



void TMainWindow::project_file_mismatch(QString rootdir, QString projectname)
{
    ProjectConverterDialog dialog(this);
    dialog.set_project(rootdir, projectname);
    dialog.exec();
}

void TMainWindow::change_recording_format_to_wav()
{
    config().set_property("Recording", "FileFormat", "wav");
    save_config_and_emit_message(tr("Changed encoding for recording to %1").arg("WAVE"));
    config().save();
}

void TMainWindow::change_recording_format_to_wav64()
{
    config().set_property("Recording", "FileFormat", "w64");
    save_config_and_emit_message(tr("Changed encoding for recording to %1").arg("WAVE-64"));
}

void TMainWindow::change_recording_format_to_wavpack()
{
    config().set_property("Recording", "FileFormat", "wavpack");
    save_config_and_emit_message(tr("Changed encoding for recording to %1").arg("WavPack"));
}

void TMainWindow::save_config_and_emit_message(const QString & message)
{
    tInformUser().information(message);
    config().save();
}

// snapping is a global property and should be stored in each sheet
void TMainWindow::snap_state_changed(bool state)
{
    if (m_project) {
        QList<TSheet* > sheetlist = m_project->get_sheets();
        foreach( TSheet* sheet, sheetlist) {
            sheet->set_snapping(state);
        }
    }
}

void TMainWindow::update_snap_state()
{
    if (m_project) {
        bool snapping = m_project->get_current_session()->is_snap_on();
        m_snapAction->setChecked(snapping);
    }
}

// scrolling is a global property but should not be stored in the sheets
void TMainWindow::update_follow_state()
{
    m_isFollowing = config().get_property("PlayHead", "Follow", true).toBool();
    m_followAction->setChecked(m_isFollowing);
}

void TMainWindow::update_temp_follow_state(bool state)
{
    if (m_project->get_current_session()->is_transport_rolling() && m_isFollowing) {
        m_followAction->setChecked(state);
    }
}

void TMainWindow::follow_state_changed(bool state)
{
    TSheet* sheet = qobject_cast<TSheet*>(m_project->get_current_session());

    if (!sheet) {
        return;
    }

    if (!sheet->is_transport_rolling() || !m_isFollowing) {
        m_isFollowing = state;
        config().set_property("PlayHead", "Follow", state);
        config().save();
        if (sheet->is_transport_rolling()) {
            sheet->set_temp_follow_state(state);
        }
    } else {
        sheet->set_temp_follow_state(state);
    }
}

TCommand* TMainWindow::show_welcome_page()
{
    m_previousCenterAreaWidgetIndex = m_centerAreaWidget->currentIndex();
    m_centerAreaWidget->setCurrentIndex(0);

    return 0;
}

TCommand* TMainWindow::show_current_sheet()
{
    m_centerAreaWidget->setCurrentIndex(m_previousCenterAreaWidgetIndex);
    return 0;
}


TCommand* TMainWindow::show_track_finder()
{
    if (!m_project) {
        return 0;
    }

    m_trackFinder->setText("Type to locate");
    m_trackFinder->selectAll();

    m_trackFinderModel.clear();

    QList<TSheet*> sheets = m_project->get_sheets();

    foreach(TSheet* sheet, sheets) {
        QList<TTrack*> tracks = sheet->get_tracks();
        tracks.append(sheet->get_master_out_bus_track());
        tracks.append(m_project->get_master_out_bus_track());
        foreach(TTrack* track, tracks) {
            QStandardItem* sItem = new QStandardItem(track->get_name());
            sItem->setData(track->get_id(), Qt::UserRole);
            QList<QStandardItem*> items;
            items.append(sItem);
            sItem = new QStandardItem(sheet->get_name());
            items.append(sItem);
            m_trackFinderModel.appendRow(items);
        }
    }

    m_trackFinderTreeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_trackFinderTreeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    m_trackFinder->setFocus();

    return 0;
}


void TMainWindow::track_finder_model_index_changed(const QModelIndex& index)
{
    qlonglong id = index.data(Qt::UserRole).toLongLong();

    foreach(TSheetWidget* sw, m_sheetWidgets) {
        TSession* session = sw->get_session();
        if (!session) return;
        TTrack* track = session->get_track(id);
        if (track) {
            show_session(session);
            sw->setFocus();
            sw->get_sheetview()->browse_to_track(track);
            break;
        }
    }
    track_finder_show_initial_text();
}

void TMainWindow::track_finder_return_pressed()
{
    if (!m_project) {
        return;
    }

    QString name = m_trackFinder->text();
    QList<QStandardItem*> items = m_trackFinderModel.findItems(name, Qt::MatchStartsWith);
    if (items.size()) {
        track_finder_model_index_changed(m_trackFinderModel.indexFromItem(items.at(0)));
        return;
    }
}

void TMainWindow::track_finder_show_initial_text()
{
    m_trackFinder->setText(tr("Track Finder"));
}

TCommand* TMainWindow::browse_to_first_track_in_active_sheet()
{
    if (m_currentSheetWidget) {
        TSheetView* sv = m_currentSheetWidget->get_sheetview();
        QList<TTrackView*> tracks = sv->get_track_views();
        if (tracks.size()) {
            sv->browse_to_track(tracks.first()->get_track());
        }
    }

    return 0;
}

TCommand* TMainWindow::browse_to_last_track_in_active_sheet()
{
    if (m_currentSheetWidget) {
        TSheetView* sv = m_currentSheetWidget->get_sheetview();
        QList<TTrackView*> tracks = sv->get_track_views();
        if (tracks.size()) {
            sv->browse_to_track(tracks.last()->get_track());
        }
    }

    return 0;
}

void TMainWindow::register_vumeter_level(AbstractVUMeterLevel *level)
{
    m_vuLevels.append(level);
}

void TMainWindow::unregister_vumeter_level(AbstractVUMeterLevel *level)
{
    m_vuLevels.removeAll(level);
}

void TMainWindow::update_vu_levels_peak()
{
    if (!m_project) {
        return;
    }


    for(int i=0; i<m_vuLevels.size(); i++) {
        m_vuLevels.at(i)->update_peak();
    }

    QList<TTrack*> tracks = m_project->get_sheet_tracks();
    tracks.append(m_project->get_tracks());
    tracks.append(m_project->get_master_out_bus_track());
    for(int i = 0; i< tracks.size(); i++) {
        QList<TVUMonitor*> monitors = tracks.at(i)->get_vumonitors();
        for (int j=0; j<monitors.size(); ++j) {
            monitors.at(j)->set_read();
        }
    }
}


void TMainWindow::reset_vu_levels_peak_hold_value()
{
    for(int i=0; i<m_vuLevels.size(); i++) {
        m_vuLevels.at(i)->reset_peak_hold_value();
    }
}

TCommand* TMainWindow::undo()
{
    TContextItem::get_undogroup()->undo();
    return 0;
}

TCommand* TMainWindow::redo()
{
    TContextItem::get_undogroup()->redo();
    return 0;
}

