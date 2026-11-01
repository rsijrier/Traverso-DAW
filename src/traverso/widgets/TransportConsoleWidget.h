/*
    Copyright (C) 2024 Remon Sijrier
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

#pragma once


#include <QToolBar>
#include <QPointer>
#include <QWidget>

#include "TTimeRef.h"

class TProject;
class TSheet;
class TSession;
class QPushButton;
class TFrameClock;

class TransportConsoleWidget : public QToolBar
{
Q_OBJECT

public:
	TransportConsoleWidget(QWidget* parent, TFrameClock* frameClock);
	~TransportConsoleWidget() override;

private:
	QPushButton*	m_timeLabel;
    TProject*	m_project;
    TSheet*		m_sheet;
	QAction*	m_toStartAction;
	QAction*	m_toLeftAction;
	QAction*	m_recAction;
	QAction*	m_playAction;
	QAction*	m_toEndAction;
	QAction*	m_toRightAction;
    QAction*    m_freeWheelingAction;
    TTimeRef	m_lastSnapPosition;
    TTimeRef    m_transportLocation;
    QPointer<TFrameClock> m_frameClock;
    uint64_t m_lastLabelFrame{0};   // the label follows about 8 frames per second

public slots:
	void set_project(TProject*);
    void set_session(TSession*);

private slots:
	void rec_toggled();

	void transport_started();
	void transport_stopped();
    void update_recording_state();
    // nowNanoseconds: the frame time, see TSession::get_heard_transport_location()
    void update_label(uint64_t nowNanoseconds);
	void update_layout();
};




