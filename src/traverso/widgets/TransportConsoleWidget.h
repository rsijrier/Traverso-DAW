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

#ifndef TRANSPORTCONSOLEWIDGET_H
#define TRANSPORTCONSOLEWIDGET_H


#include <QToolBar>
#include <QWidget>

#include "TTimeRef.h"

class Project;
class Sheet;
class TSession;
class QPushButton;

class TransportConsoleWidget : public QToolBar
{
Q_OBJECT

public:
	TransportConsoleWidget(QWidget* parent);

private:
	QPushButton*	m_timeLabel;
    Project*	m_project;
    Sheet*		m_sheet;
	QAction*	m_toStartAction;
	QAction*	m_toLeftAction;
	QAction*	m_recAction;
	QAction*	m_playAction;
	QAction*	m_toEndAction;
	QAction*	m_toRightAction;
    TTimeRef	m_lastSnapPosition;
    TTimeRef    m_transportLocation;
    trav_time_t m_lastTransportLocationUpdatetime;

public slots:
	void set_project(Project*);
    void set_session(TSession*);

private slots:
	void to_start();
	void to_left();
	void rec_toggled();
	void play_toggled();
	void to_end();
	void to_right();

	void transport_started();
	void transport_stopped();
	void update_recording_state();
	void update_label();
	void update_layout();
};



#endif

