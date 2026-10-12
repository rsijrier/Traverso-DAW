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

#include "TTimeLineMarker.h"
#include "TSheet.h"
#include "TLocation.h"
#include "TTimeLineRuler.h"
#include "Utils.h"

TTimeLineMarker::TTimeLineMarker(TTimeLineRuler* tl, const TTimeRef when, MarkerType type)
	: TContextItem(tl)
    , m_timeline(tl)
    , m_location(new TLocation(this))
	, m_type(type)
{
    QObject::tr("TTimeLineMarker");
    set_history_stack(m_timeline->get_history_stack());

    m_location->set_snap_list(m_timeline->get_sheet()->get_snap_list());
    m_location->set_location(this, when, when);

    m_description = "";
    m_performer = "";
    m_composer = "";
    m_arranger = "";
    m_message = "";
    m_isrc = "";
    m_preemph = false;
    m_copyprotect = false;
    m_index = -1;
}

TTimeLineMarker::TTimeLineMarker(TTimeLineRuler * tl, const QDomNode& node)
	: TContextItem(tl)
    , m_timeline(tl)
    , m_location(new TLocation(this))
{
    m_location->set_snap_list(m_timeline->get_sheet()->get_snap_list());
    set_history_stack(m_timeline->get_history_stack());
	set_state(node);
}

QDomNode TTimeLineMarker::get_state(QDomDocument doc)
{
    QDomElement domNode = doc.createElement("Marker");

    domNode.setAttribute("position",  m_location->get_start().universal_frame());
    domNode.setAttribute("description",  m_description);
    domNode.setAttribute("id",  get_id());
    domNode.setAttribute("performer", m_performer);
    domNode.setAttribute("composer", m_composer);
    domNode.setAttribute("songwriter", m_songwriter);
    domNode.setAttribute("arranger", m_arranger);
    domNode.setAttribute("message", m_message);
    domNode.setAttribute("isrc", m_isrc);
    domNode.setAttribute("preemphasis", m_preemph);
    domNode.setAttribute("copyprotection", m_copyprotect);

    switch (m_type) {
    case CDTRACK:
        domNode.setAttribute("type",  "CDTRACK");
        break;
    case ENDMARKER:
        domNode.setAttribute("type",  "ENDMARKER");
        break;
    case PLAYHEADMARKER:
        break;
    }

    return domNode;
}

int TTimeLineMarker::set_state(const QDomNode & node)
{
    QDomElement e = node.toElement();

    m_description = e.attribute("description", "");
    QString tp = e.attribute("type", "CDTRACK");
    TTimeRef location = TTimeRef(e.attribute("position", "0").toLongLong());
    m_location->set_location(this, location, location);
    set_id(e.attribute("id", "0").toLongLong());
    m_performer = e.attribute("performer", "");
    m_composer = e.attribute("composer", "");
    m_songwriter = e.attribute("songwriter", "");
    m_arranger = e.attribute("arranger", "");
    m_message = e.attribute("message", "");
    m_isrc = e.attribute("isrc", "");
    m_preemph = e.attribute("preemphasis", "0").toInt();
    m_copyprotect = e.attribute("copyprotection", "0").toInt();

    if (tp == "CDTRACK") m_type = CDTRACK;
    if (tp == "ENDMARKER") m_type = ENDMARKER;

    return 1;
}

void TTimeLineMarker::set_when(const TTimeRef& when)
{
    m_location->set_location(this, when, when);
}

void TTimeLineMarker::set_description(const QString &s)
{
	if (m_description == s) return;
	m_description = s;
	emit descriptionChanged();
}

void TTimeLineMarker::set_performer(const QString &s)
{
	m_performer = s;
}

void TTimeLineMarker::set_composer(const QString &s)
{
	m_composer = s;
}

void TTimeLineMarker::set_songwriter(const QString &s)
{
	m_songwriter = s;
}

void TTimeLineMarker::set_arranger(const QString &s)
{
	m_arranger = s;
}

void TTimeLineMarker::set_message(const QString &s)
{
	m_message = s;
}

void TTimeLineMarker::set_isrc(const QString &s)
{
	m_isrc = s;
}

void TTimeLineMarker::set_preemphasis(bool b)
{
	m_preemph = b;
}

void TTimeLineMarker::set_copyprotect(bool b)
{
	m_copyprotect = b;
}

bool TTimeLineMarker::get_preemphasis()
{
	return m_preemph;
}

bool TTimeLineMarker::get_copyprotect()
{
	return m_copyprotect;
}

void TTimeLineMarker::set_index(int i)
{
	m_index = i;
	emit indexChanged();
}
