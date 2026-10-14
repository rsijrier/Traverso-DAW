/*
    Copyright (C) 2024 Remon Sijrier
    Copyright (C) 2008 Nicola Doebelin

    This file is part of Traverso

    Traverso is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "TransportConsoleWidget.h"

#include "TAudioDevice.h"
#include "TSheet.h"
#include "TProjectManager.h"
#include "TProject.h"
#include "TConfig.h"
#include "TInformUser.h"
#include "TTransport.h"

#include <QAction>
#include <QPushButton>
#include <QFont>
#include <QString>

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

    // 1. Skip to Start: theme icon, own SVG as fallback
    m_toStartAction = addAction(
        QIcon::fromTheme(QIcon::ThemeIcon::MediaSkipBackward, QIcon(QStringLiteral(":/icons/skip-start.svg"))),
        tr("Skip to Start")
        );
    connect(m_toStartAction, &QAction::triggered, &transport(), &TTransport::to_start);

    // 2. Previous Snap: no direct theme equivalent, own SVG as fallback
    m_toLeftAction = addAction(
        QIcon::fromTheme(QIcon::ThemeIcon::MediaSeekBackward, QIcon(QStringLiteral(":/icons/seek-prev.svg"))),
        tr("Previous Snap Position")
        );
    connect(m_toLeftAction, &QAction::triggered, &transport(), &TTransport::prev_skip_pos);

    // 3. Record: theme icon, own SVG as fallback
    m_recAction = addAction(
        QIcon::fromTheme(QIcon::ThemeIcon::MediaRecord, QIcon(QStringLiteral(":/icons/record.svg"))),
        tr("Record")
        );
    connect(m_recAction, &QAction::triggered, this, &TransportConsoleWidget::rec_toggled);

    // 4. Play / Stop: theme icon, own SVG as fallback
    m_playAction = addAction(
        QIcon::fromTheme(QIcon::ThemeIcon::MediaPlaybackStart, QIcon(QStringLiteral(":/icons/play.svg"))),
        tr("Play / Stop")
        );
    connect(m_playAction, &QAction::triggered, &transport(), &TTransport::start_transport);

    // 5. Next Snap: own SVG as fallback
    m_toRightAction = addAction(
        QIcon::fromTheme(QIcon::ThemeIcon::MediaSeekForward, QIcon(QStringLiteral(":/icons/seek-prev.svg"))),
        tr("Next Snap Position")
        );
    connect(m_toRightAction, &QAction::triggered, &transport(), &TTransport::next_skip_pos);

    // 6. Skip to End: theme icon, own SVG as fallback
    m_toEndAction = addAction(
        QIcon::fromTheme(QIcon::ThemeIcon::MediaSkipForward, QIcon(QStringLiteral(":/icons/skip-end.svg"))),
        tr("Skip to End")
        );
    connect(m_toEndAction, &QAction::triggered, &transport(), &TTransport::to_end);

    m_freeWheelingAction = addAction(QStringLiteral("RT"), tr("Start/Stop FreeWheeling"), this, [](){
        audiodevice().set_free_wheeling(audiodevice().running_real_time());
    });
    m_freeWheelingAction->setCheckable(true);
    m_freeWheelingAction->setToolTip(tr("Turn Free Wheeling ON"));

    addWidget(m_timeLabel);

    m_recAction->setCheckable(true);
    m_playAction->setCheckable(true);

    m_lastSnapPosition = TTimeRef();

    connect(&pm(), &TProjectManager::projectLoaded, this, &TransportConsoleWidget::set_project);
    connect(&audiodevice(), &TAudioDevice::finishedOneProcessCycle, this, &TransportConsoleWidget::update_label);

    connect(&audiodevice(), &TAudioDevice::freeWheelingChanged, this, [this](){
        m_freeWheelingAction->setChecked(!audiodevice().running_real_time());
        if (audiodevice().running_real_time()) {
            m_freeWheelingAction->setText(QStringLiteral("RT"));
            m_freeWheelingAction->setToolTip(tr("Turn Free Wheeling ON"));
        } else {
            m_freeWheelingAction->setText(QStringLiteral("FW"));
            m_freeWheelingAction->setToolTip(tr("Turn Free Wheeling OFF"));
        }
    });

    update_layout();
    update_label();
}

void TransportConsoleWidget::set_project(TProject* project)
{
    if (m_project) {
        disconnect(m_project, &TProject::currentSessionChanged, this, &TransportConsoleWidget::set_session);
    }

    m_project = project;

    if (m_project) {
        connect(m_project, &TProject::currentSessionChanged, this, &TransportConsoleWidget::set_session);
    } else {
        set_session(nullptr);
    }
}

void TransportConsoleWidget::set_session(TSession* session)
{
    TProject* project = qobject_cast<TProject*>(session);
    if (project) {
        return;
    }

    if (m_sheet) {
        disconnect(m_sheet, &TSheet::recordingStateChanged, this, &TransportConsoleWidget::update_recording_state);
        disconnect(m_sheet, &TSheet::transportStarted, this, &TransportConsoleWidget::transport_started);
        disconnect(m_sheet, &TSheet::transportStopped, this, &TransportConsoleWidget::transport_stopped);
    }

    m_sheet = qobject_cast<TSheet*>(session);
    if (!m_sheet && session) {
        m_sheet = qobject_cast<TSheet*>(session->get_parent_session());
    }

    if (!m_sheet) {
        setEnabled(false);
        update_label();
        return;
    }

    setEnabled(true);

    connect(m_sheet, &TSheet::recordingStateChanged, this, &TransportConsoleWidget::update_recording_state);
    connect(m_sheet, &TSheet::transportStarted, this, &TransportConsoleWidget::transport_started);
    connect(m_sheet, &TSheet::transportStopped, this, &TransportConsoleWidget::transport_stopped);
}

void TransportConsoleWidget::rec_toggled()
{
    if (m_sheet) {
        m_sheet->set_recordable();
    }
}

void TransportConsoleWidget::transport_started()
{
    m_playAction->setChecked(true);
    // Switch to the Stop icon, own SVG as fallback
    m_playAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::MediaPlaybackStop, QIcon(QStringLiteral(":/icons/stop.svg"))));
    m_recAction->setEnabled(false);

    if (m_sheet && !m_sheet->is_recording()) {
        m_recAction->setChecked(false);
    }
}

void TransportConsoleWidget::transport_stopped()
{
    m_playAction->setChecked(false);
    // Switch back to the Play icon, own SVG as fallback
    m_playAction->setIcon(QIcon::fromTheme(QIcon::ThemeIcon::MediaPlaybackStart, QIcon(QStringLiteral(":/icons/play.svg"))));
    m_recAction->setEnabled(true);
}

void TransportConsoleWidget::update_recording_state()
{
    if (!m_sheet) {
        return;
    }

    if (m_sheet->is_recording()) {
        QString recordFormat = config().get_property("Recording", "FileFormat", "wav").toString();
        tInformUser().information(tr("Recording to %1 Tracks, encoding format: %2")
                                      .arg(m_sheet->get_armed_tracks().size())
                                      .arg(recordFormat));
        m_recAction->setChecked(true);
    } else {
        m_recAction->setChecked(false);
    }
}

void TransportConsoleWidget::update_label()
{
    auto newUpdateTime = TTimeRef::get_milliseconds_since_epoch();

    // 8 frames/sec limiet
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
