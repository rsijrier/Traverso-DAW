/*
Copyright (C) 2007 Remon Sijrier 

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


#ifndef MARKER_H
#define MARKER_H

#include "ContextItem.h"
#include "TLocation.h"
#include <QDomNode>

class TTimeLineRuler;

class Marker : public ContextItem
{
	Q_OBJECT
	
public:
    enum MarkerType {
		CDTRACK,
		ENDMARKER
	};

    Marker(TTimeLineRuler* tl, const TTimeRef when, MarkerType type = CDTRACK);
	Marker(TTimeLineRuler* tl, const QDomNode& node);
        ~Marker() {}
	
	QDomNode get_state(QDomDocument doc);
	int set_state(const QDomNode& node);

	void set_description(const QString &);
	void set_performer(const QString &);
	void set_composer(const QString &);
	void set_songwriter(const QString &);
	void set_arranger(const QString &);
	void set_message(const QString &);
	void set_isrc(const QString &);
	void set_preemphasis(bool);
	void set_copyprotect(bool);
	void set_index(int);

	TTimeLineRuler * get_timeline() const {return m_timeline;}
	TTimeRef get_when() const {return m_when;}
    TLocation* get_location() const {return m_location;}

	QString get_description() const {return m_description;}
	QString get_performer() const {return m_performer;}
	QString get_composer() const {return m_composer;}
	QString get_songwriter() const {return m_songwriter;}
	QString get_arranger() const {return m_arranger;}
	QString get_message() const {return m_message;}
	QString get_isrc() const {return m_isrc;}
	bool get_preemphasis();
	bool get_copyprotect();
    MarkerType get_type() {return m_type;}
    int get_index() {return m_index;}

public slots:
	void set_when (const TTimeRef& when);


private:
	TTimeLineRuler* m_timeline;
    TTimeRef        m_when;
    TLocation*      m_location;
	QString	m_description,
    m_performer,
    m_composer,
    m_songwriter,
    m_arranger,
    m_message,
    m_isrc;
    bool	m_preemph{};
    bool	m_copyprotect{};
    MarkerType	m_type;
    int     m_index{};
	
signals:
	void positionChanged();
	void descriptionChanged();
	void indexChanged();
};

#endif

//eof
