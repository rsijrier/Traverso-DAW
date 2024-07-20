/*
    Copyright (C) 2008 Nicola Doebelin

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

#include "TransportConsoleWidget.h"

#include "AudioDevice.h"
#include "Sheet.h"
#include "Utils.h"
#include "ProjectManager.h"
#include "Project.h"
#include "TConfig.h"
#include "Information.h"


#include <QAction>
#include <QPushButton>
#include <QFont>
#include <QString>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


TransportConsoleWidget::TransportConsoleWidget(QWidget* parent)
	: QToolBar(parent)
{
    setEnabled(false);

    m_project = nullptr;
    m_sheet = nullptr;

    m_transportLocation = TTimeRef();
    m_lastTransportLocationUpdatetime = 0;

    m_timeLabel = new QPushButton(this);
    m_timeLabel->setFocusPolicy(Qt::NoFocus);
    m_timeLabel->setStyleSheet(
        "color: lime;"
        "background-color: black;"
        "font: 19px;"
        "border: 2px solid gray;"
        "border-radius: 10px;"
        "padding: 0 8 0 8;");

    m_toStartAction = addAction(QIcon(":/skipleft"), tr("Skip to Start"), this, SLOT(to_start()));
    m_toLeftAction = addAction(QIcon(":/seekleft"), tr("Previous Snap Position"), this, SLOT(to_left()));
    m_recAction = addAction(QIcon(":/record"), tr("Record"), this, SLOT(rec_toggled()));
    m_playAction = addAction(QIcon(":/playstart"), tr("Play / Stop"), this, SLOT(play_toggled()));
    m_toRightAction = addAction(QIcon(":/seekright"), tr("Next Snap Position"), this, SLOT(to_right()));
    m_toEndAction = addAction(QIcon(":/skipright"), tr("Skip to End"), this, SLOT(to_end()));

    addWidget(m_timeLabel);

    m_recAction->setCheckable(true);
    m_playAction->setCheckable(true);

    m_lastSnapPosition = TTimeRef();

    connect(&pm(), SIGNAL(projectLoaded(Project*)), this, SLOT(set_project(Project*)));
    connect(&audiodevice(), SIGNAL(finishedOneProcessCycle()), this, SLOT(update_label()));

    update_layout();
    update_label();
}


void TransportConsoleWidget::set_project(Project* project)
{
    if (m_project) {
        disconnect(m_project, SIGNAL(currentSessionChanged(TSession*)), this, SLOT(set_session(TSession*)));
    }

    m_project = project;

    if (m_project) {
        connect(m_project, SIGNAL(currentSessionChanged(TSession*)), this, SLOT(set_session(TSession*)));
    } else {
        set_session(nullptr);
    }
}

void TransportConsoleWidget::set_session(TSession* session)
{
    Project* project = qobject_cast<Project*>(session);
    // if the view was changed to Project's session (mixer)
    // then keep the current active sheet!
    if (project) {
        return;
    }

    if (m_sheet) {
        disconnect(m_sheet, SIGNAL(recordingStateChanged()), this, SLOT(update_recording_state()));
        disconnect(m_sheet, SIGNAL(transportStarted()), this, SLOT(transport_started()));
        disconnect(m_sheet, SIGNAL(transportStopped()), this, SLOT(transport_stopped()));

    }

    m_sheet = qobject_cast<Sheet*>(session);
    if (!m_sheet && session) {
        m_sheet = qobject_cast<Sheet*>(session->get_parent_session());
    }

    if (!m_sheet) {
        setEnabled(false);
        update_label();
        return;
    }

    setEnabled(true);

    connect(m_sheet, SIGNAL(recordingStateChanged()), this, SLOT(update_recording_state()));
    connect(m_sheet, SIGNAL(transportStarted()), this, SLOT(transport_started()));
    connect(m_sheet, SIGNAL(transportStopped()), this, SLOT(transport_stopped()));
}

void TransportConsoleWidget::to_start()
{
    m_sheet->skip_to_start();
}

void TransportConsoleWidget::to_left()
{
    m_sheet->prev_skip_pos();
}

void TransportConsoleWidget::rec_toggled()
{
	m_sheet->set_recordable();
}

void TransportConsoleWidget::play_toggled()
{
	m_sheet->start_transport();
}

void TransportConsoleWidget::to_end()
{
    m_sheet->skip_to_end();
}

void TransportConsoleWidget::to_right()
{
    m_sheet->next_skip_pos();
}

void TransportConsoleWidget::transport_started()
{
    m_playAction->setChecked(true);
    m_playAction->setIcon(QIcon(":/playstop"));
    m_recAction->setEnabled(false);

	// this is needed when the record button is pressed, but no track is armed.
	// uncheck the rec button in that case
    if (m_sheet && !m_sheet->is_recording()) {
        m_recAction->setChecked(false);
    }
}

void TransportConsoleWidget::transport_stopped()
{
    m_playAction->setChecked(false);
    m_playAction->setIcon(QIcon(":/playstart"));
    m_recAction->setEnabled(true);
}

void TransportConsoleWidget::update_recording_state()
{
	if (!m_sheet)
	{
		return;
	}

    if (m_sheet->is_recording()) {
        QString recordFormat = config().get_property("Recording", "FileFormat", "wav").toString();
        info().information(tr("Recording to %1 Tracks, encoding format: %2").arg(m_sheet->get_armed_tracks().size()).arg(recordFormat));
        m_recAction->setChecked(true);
    } else {
        m_recAction->setChecked(false);
    }
}

void TransportConsoleWidget::update_label()
{
    auto newUpdateTime = TTimeRef::get_milliseconds_since_epoch();

    // Limit the updating of the label to 8 frames/sec
    if ((newUpdateTime - m_lastTransportLocationUpdatetime) < 125) {
        return;
    }

    m_lastTransportLocationUpdatetime = newUpdateTime;

    TTimeRef newTransportLocation(TTimeRef::INVALID);

    if (!m_sheet) {
        if (m_transportLocation != TTimeRef::INVALID) {
            m_transportLocation = TTimeRef::INVALID;
            m_timeLabel->setText(TTimeRef::timeref_to_ms_2(m_transportLocation));
        }
    } else {
        newTransportLocation = m_sheet->get_transport_location();
    }


    if (m_transportLocation == newTransportLocation) {
        return;
    }

    m_transportLocation = newTransportLocation;

    m_timeLabel->setText(TTimeRef::timeref_to_ms_2(m_transportLocation));
}

void TransportConsoleWidget::update_layout()
{
	int iconsize = config().get_property("Themer", "transportconsolesize", "22").toInt();
	setIconSize(QSize(iconsize, iconsize));
}

//eof

