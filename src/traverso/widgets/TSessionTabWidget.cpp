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

#include "TSessionTabWidget.h"
#include "TWorkspace.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QApplication>

#include "TProjectManager.h"
#include "TProject.h"
#include "TSession.h"
#include "TSheet.h"
#include "TMainWindow.h"


static const int HOR_BUTTON_HEIGHT = 30;
static const int VER_BUTTON_HEIGHT = 28;
static const int LABEL_WIDTH = 80;
static const int TAB_WIDTH = 110;


// QToolBar items don't like to have a parent widget. Moving the toolbar
// to another area crashes T when closing a project when adding a parent widget
// to the QPushButton constructor!!
TSessionTabWidget::TSessionTabWidget(QToolBar* toolBar, TSession *session)
    : QPushButton(toolBar)
{
    m_toolBar = toolBar;
    m_session = session;

    m_spacer = new QWidget(this);
    m_spacer->setMinimumWidth(4);
    m_spacer->setMinimumHeight(4);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setMinimumWidth(LABEL_WIDTH);

    session_property_changed();

    m_arrowButton = new QPushButton(this);
    m_arrowButton->setFixedSize(TAB_WIDTH - LABEL_WIDTH, HOR_BUTTON_HEIGHT - 8);
    update_arrow_button_shortcut_and_icon();

    // IMPORTANT: this menu needs MainWindow as parent, if the 'close space'
    // action is triggered, mouse events are dispatched correctly by Qt so
    // that the menu close vs view deletion vs context pointer set_view_port()
    // is processed in the correct order.
    m_arrowButtonMenu = new QMenu(TMainWindow::instance());
    m_arrowButtonMenu->installEventFilter(TMainWindow::instance());
    connect(m_arrowButton, &QToolButton::clicked, this, &TSessionTabWidget::arrow_button_clicked);

    m_childLayout = new QHBoxLayout;

    m_childLayout->addSpacing(6);
    m_childLayout->addWidget(m_nameLabel);
    m_childLayout->addStretch(1);
    m_childLayout->addWidget(m_arrowButton);
    m_childLayout->addSpacing(4);
    m_childLayout->setContentsMargins(0, 0, 0, 0);

    QAction* action;

    if ( ! m_session->is_child_session()) {
        m_mainWidget = new QWidget(this);
        m_mainWidget->setLayout(m_childLayout);
        // m_mainWidget->setStyleSheet("background-color: none;");

        toolbar_orientation_changed(toolBar->orientation());

        if (m_session->is_project_session()) {
            action = m_arrowButtonMenu->addAction(tr("Edit..."));
            connect(action, &QAction::triggered, TMainWindow::instance(), &TMainWindow::show_project_manager_dialog);
            action = m_arrowButtonMenu->addSeparator();
        }

        action = m_arrowButtonMenu->addAction(tr("New Track / Bus..."));
        action->setIcon(TMainWindow::find_pixmap(":/new"));
        connect(action, &QAction::triggered, this, &TSessionTabWidget::add_track_action_triggered);

        if (m_session->is_project_session()) {
            action = m_arrowButtonMenu->addAction(tr("New Sheet..."));
            action->setIcon(TMainWindow::find_pixmap(":/new"));
            connect(action, &QAction::triggered, TMainWindow::instance(), &TMainWindow::show_newsheet_dialog);
        }

        action = m_arrowButtonMenu->addAction(tr("New WorkSpace..."));
        action->setIcon(TMainWindow::find_pixmap(":/new"));
        connect(action, &QAction::triggered, this, &TSessionTabWidget::add_new_work_space_action_triggered);

        if (m_session->is_project_session()) {

            action = m_arrowButtonMenu->addSeparator();

            action = m_arrowButtonMenu->addAction(tr("Close Project"));
            action->setIcon(QIcon(":/exit"));
            connect(action, &QAction::triggered, this, &TSessionTabWidget::close_current_project);
        }

        // m_nameLabel->setStyleSheet("font-size: 12px;");
    }

    foreach(TSession* session, m_session->get_child_sessions()) {
        child_session_added(session);
    }

    if (m_session->is_child_session()) {
        setLayout(m_childLayout);

        action = m_arrowButtonMenu->addAction(tr("Edit..."));
        connect(action, &QAction::triggered, this, &TSessionTabWidget::add_track_action_triggered);

        m_arrowButtonMenu->addSeparator();
        action = m_arrowButtonMenu->addAction(QIcon(":/exit"), tr("Close WorkSpace"));
        action->setIcon(QIcon(":/exit"));
        connect(action, &QAction::triggered, this, &TSessionTabWidget::close_action_triggered);
    }


    calculate_size();

    connect(session, &TSession::transportStarted, this, &TSessionTabWidget::session_transport_started);
    connect(session, &TSession::transportStopped, this, &TSessionTabWidget::session_transport_stopped);
    connect(session, &TSession::sessionAdded, this, &TSessionTabWidget::child_session_added);
    connect(session, &TSession::sessionRemoved, this, &TSessionTabWidget::child_session_removed);
    connect(session, &TSession::propertyChanged, this, &TSessionTabWidget::session_property_changed);
    connect(m_toolBar, &QToolBar::orientationChanged, this, &TSessionTabWidget::toolbar_orientation_changed);
    connect(this, &TSessionTabWidget::clicked, this, &TSessionTabWidget::button_clicked);
    connect(pm().get_project(), &TProject::currentSessionChanged, this, &TSessionTabWidget::project_current_session_changed);
    connect(pm().get_project(), &TProject::sessionIsAlreadyCurrent, this, &TSessionTabWidget::project_session_is_current);

    if (pm().get_project()->get_current_session() == m_session) {
        project_current_session_changed(m_session);
    }
}

// void TSessionTabWidget::leaveEvent( QEvent * )
// {
//     // m_arrowButton->setStyleSheet("background-color: none; border: none;");
// }

// void TSessionTabWidget::enterEvent(QEnterEvent * /*e*/)
// {
//     // if (pm().get_project()->get_current_session() == m_session) {
//     //     m_arrowButton->setStyleSheet("background-color: darkGray; margin: 0; margin-right: 2;");
//     // }
// }

void TSessionTabWidget::toolbar_orientation_changed(Qt::Orientation orientation)
{
    if ( ! m_session->is_child_session()) {
        foreach(TSessionTabWidget* tabWidget, m_childTabWidgets) {
            layout()->removeWidget(tabWidget);
        }

        if (layout()) {
            layout()->removeWidget(m_mainWidget);
            layout()->removeWidget(m_spacer);
        }

        delete layout();

        if (orientation == Qt::Vertical) {
            QVBoxLayout* vLayout = new QVBoxLayout();
            vLayout->setContentsMargins(0, 0, 0, 0);
            setLayout(vLayout);
        } else {
            QHBoxLayout* hLayout = new QHBoxLayout();
            hLayout->setContentsMargins(0, 0, 0, 0);
            setLayout(hLayout);
        }

        layout()->addWidget(m_mainWidget);

        foreach(TSessionTabWidget* tabWidget, m_childTabWidgets) {
            layout()->addWidget(tabWidget);
        }

        if (m_childTabWidgets.size()) {
            layout()->addWidget(m_spacer);
        }

        calculate_size();
    }
}

void TSessionTabWidget::child_session_added(TSession *session)
{
    TSessionTabWidget* tabWidget = new TSessionTabWidget(m_toolBar, session);
    m_childTabWidgets.append(tabWidget);
    layout()->removeWidget(m_spacer);
    layout()->addWidget(tabWidget);
    layout()->addWidget(m_spacer);

    calculate_size();
}

void TSessionTabWidget::child_session_removed(TSession *session)
{
    foreach(TSessionTabWidget* tabWidget, m_childTabWidgets) {
        if (tabWidget->get_session() == session) {
            layout()->removeWidget(tabWidget);
            m_childTabWidgets.removeAll(tabWidget);
            delete tabWidget;
            break;
        }
    }
    if (!m_childTabWidgets.size()) {
        layout()->removeWidget(m_spacer);
    }

    calculate_size();
}

void TSessionTabWidget::calculate_size()
{
    if (!m_session->is_child_session()) {
        if (m_toolBar->orientation() == Qt::Vertical) {
            setFixedSize(TAB_WIDTH, VER_BUTTON_HEIGHT + 4 + m_session->get_child_sessions().count() * VER_BUTTON_HEIGHT);
        } else {
            setFixedSize(TAB_WIDTH + 4 + m_session->get_child_sessions().count() * (TAB_WIDTH + 4), HOR_BUTTON_HEIGHT);
        }
        foreach(TSessionTabWidget* tabWidget, m_childTabWidgets) {
            if (m_toolBar->orientation() == Qt::Vertical) {
                tabWidget->setMinimumSize(TAB_WIDTH - 4, VER_BUTTON_HEIGHT);
            } else {
                tabWidget->setMinimumSize(TAB_WIDTH, HOR_BUTTON_HEIGHT - 4);
            }
        }
    }
}

void TSessionTabWidget::session_property_changed()
{
    if (m_session->is_project_session()) {
        m_nameLabel->setText(tr("Mixer"));
    } else {
        m_nameLabel->setText(m_session->get_name());

    }
}

void TSessionTabWidget::button_clicked()
{
    if (pm().get_project()->get_current_session() != m_session) {
        pm().get_project()->set_current_session(m_session);
        enterEvent(nullptr);
    } else {
        arrow_button_clicked();
    }
}

void TSessionTabWidget::arrow_button_clicked()
{
    if (pm().get_project()->get_current_session() == m_session) {
        m_arrowButtonMenu->move(QCursor::pos());
        m_arrowButtonMenu->show();
    } else {
        shortcut_click();
    }
}

void TSessionTabWidget::shortcut_click()
{
    animateClick();
}

void TSessionTabWidget::close_action_triggered()
{
    pm().get_project()->remove_child_session();
}

void TSessionTabWidget::add_track_action_triggered()
{
    TMainWindow::instance()->show_newtrack_dialog();
}

void TSessionTabWidget::add_new_work_space_action_triggered()
{
    TMainWindow::instance()->show_add_child_session_dialog();
}

void TSessionTabWidget::session_transport_started()
{
    TProject* project = pm().get_project();
    if (!project) {
        return;
    }

    TSession* activeSession = project->get_current_session();
    TSession* activeSong = static_cast<TSession*>(project->get_active_sheet());

    if (activeSession == m_session) {
        m_nameLabel->setStyleSheet("color: #FF5E00;");
        return;
    }

    if (activeSong && activeSong == m_session) {
        m_nameLabel->setStyleSheet("color: #FF5E00;");
    }
}

void TSessionTabWidget::session_transport_stopped()
{
    m_nameLabel->setStyleSheet("");
}

void TSessionTabWidget::project_current_session_changed(TSession *session)
{
    if (!session) {
        return;
    }

    QFont font = m_nameLabel->font();

    if (session == m_session) {
        font.setBold(true);
        m_nameLabel->setFont(font);
    } else {
        font.setBold(false);
        m_nameLabel->setFont(font);
    }

    update_arrow_button_shortcut_and_icon();
}


void TSessionTabWidget::project_session_is_current(TSession *session)
{
    if (session == m_session) {
        arrow_button_clicked();
    }
}

void TSessionTabWidget::update_arrow_button_shortcut_and_icon()
{
    int number = pm().get_project()->get_session_index(m_session) + 1;
    if (number < 10) {
        m_arrowButton->setText(QString("&%1").arg(number));
    } else {
        m_arrowButton->setText(QString("%1").arg(number));
    }
}

void TSessionTabWidget::close_current_project()
{
    // for some reason if we have focus, and the project closes
    // all it's views, Qt crashes somewhere in it's widget backingstore
    // so unset the focus first to the main window, seems to help :)
    TMainWindow::instance()->setFocus(Qt::MouseFocusReason);
    qApp->processEvents();
    pm().close_current_project();
}

