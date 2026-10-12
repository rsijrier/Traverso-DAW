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

#include "TSession.h"

#include "TAudioDevice.h"
#include "TAddRemoveCommand.h"
#include "TAudioTrack.h"
#include "TConfig.h"
#include "TPeak.h"
#include "Utils.h"
#include "TBusTrack.h"
#include "TSheet.h"
#include "TSnapList.h"
#include "TLocation.h"
#include "TTimeLineRuler.h"

#include "Debugger.h"
#include <qpoint.h>

TSession::TSession(TSession *parentSession)
	: TContextItem()
    , m_curveProcessBuffer(audiodevice().get_buffer_size())
{
	m_parentSession = nullptr;
    m_masterOutBusTrack = nullptr;
    m_bounceTrack = nullptr;
    m_snaplist = nullptr;
    m_timeLineRuler = nullptr;
    m_workSnap = nullptr;

	if (!parentSession) {
        m_timeLineRuler = new TTimeLineRuler(this);
		m_snaplist = new TSnapList(this);
        m_workSnap = new TLocation(this);
	} else {
		set_parent_session(parentSession);
	}

    // TODO seek to old position on project exit ?
    m_workLocation = TTimeRef();
    m_transportLocation = TTimeRef();
    m_scrollBarXValue = m_scrollBarYValue = 0;
    m_hzoom = config().get_property("Sheet", "hzoomLevel", 8192).toInt();
    set_transport_rolling_state(false);
    m_isSnapOn=true;

    connect(this, &TSession::privateTrackAdded, this, &TSession::private_track_added);
    connect(this, &TSession::privateTrackRemoved, this, &TSession::private_track_removed);
}

TSession::~TSession()
{
    delete m_snaplist;
}

void TSession::set_parent_session(TSession *parentSession)
{
//        if (m_isProjectSession) {

		if (m_parentSession) {
			disconnect(m_parentSession, &TSession::transportStarted, this, &TSession::transportStarted);
			disconnect(m_parentSession, &TSession::transportStopped, this, &TSession::transportStopped);
            disconnect(m_parentSession, &TSession::transportLocationChanged, this, &TSession::transportLocationChanged);
			disconnect(m_parentSession, &TSession::workingPosChanged, this, &TSession::workingPosChanged);
			disconnect(m_parentSession, &TSession::hzoomChanged, this, &TSession::hzoomChanged);
			disconnect(m_parentSession, &TSession::horizontalScrollBarValueChanged, this, &TSession::horizontalScrollBarValueChanged);
		}
		connect(parentSession, &TSession::transportStarted, this, &TSession::transportStarted);
		connect(parentSession, &TSession::transportStopped, this, &TSession::transportStopped);
        connect(parentSession, &TSession::transportLocationChanged, this, &TSession::transportLocationChanged);
		connect(parentSession, &TSession::workingPosChanged, this, &TSession::workingPosChanged);
		connect(parentSession, &TSession::hzoomChanged, this, &TSession::hzoomChanged);
		connect(parentSession, &TSession::horizontalScrollBarValueChanged, this, &TSession::horizontalScrollBarValueChanged);
//        }

	m_parentSession = parentSession;

    if (!m_isProjectSession) {
        set_history_stack(m_parentSession->get_history_stack());
    }

	emit horizontalScrollBarValueChanged();
	emit hzoomChanged();
}


int TSession::set_state( const QDomNode & node )
{
	PENTER;

	QDomElement e = node.toElement();

	m_name = e.attribute("name", "" );
    set_id(e.attribute("id", "0").toLongLong());
    m_scrollBarXValue = e.attribute("sbx", "0").toInt();
    m_scrollBarYValue = e.attribute("sby", "0").toInt();

	QDomNode tracksNode = node.firstChildElement("Tracks");
	QDomNode trackNode = tracksNode.firstChild();

	while(!trackNode.isNull()) {
		QDomElement e = trackNode.toElement();

		qint64 id = e.attribute("id", "0").toLongLong();

        TTrack* track = m_parentSession->get_track(id);
		if (track) {
            TCommand* cmd = add_track(track);
            // add_track(track) should not return a TCommand object
            // but directly add track and return nullptr, this is by design
            Q_ASSERT(!cmd);
            set_track_height(track->get_id(), e.attribute("height", "90").toInt());
		}

		trackNode = trackNode.nextSibling();
	}


	return 1;
}

QDomNode TSession::get_state(QDomDocument doc)
{
	QDomElement sheetNode = doc.createElement("WorkSheet");

    sheetNode.setAttribute("id", get_id());
	sheetNode.setAttribute("name", m_name);
	sheetNode.setAttribute("sbx", get_scrollbar_xy().x());
	sheetNode.setAttribute("sby", get_scrollbar_xy().y());

	QDomNode tracksNode = doc.createElement("Tracks");

    foreach(TTrack* track, get_tracks()) {
		QDomElement trackNode = doc.createElement("Track");

		trackNode.setAttribute("id", track->get_id() );
		trackNode.setAttribute("height", get_track_height(track->get_id()));
		tracksNode.appendChild(trackNode);
	}

	sheetNode.appendChild(tracksNode);

	return sheetNode;
}



TBusTrack* TSession::get_master_out_bus_track() const
{
    if (is_project_session()) {
        return m_masterOutBusTrack;
    }

	if (m_parentSession) {
        return m_parentSession->get_master_out_bus_track();
	}

	return m_masterOutBusTrack;
}
QList<TTrack*> TSession::get_tracks() const
{
    QList<TTrack*> list;
    for(TAudioTrack* track : m_audioTracks) {
		list.append(track);
	}
    for(TBusTrack* track : m_busTracks) {
		list.append(track);
	}

    // if (m_bounceTrack) {
    //     m_bounceTrack->set_sort_index(0);
    //     list.append(m_bounceTrack);
    // }

	return list;
}

TTrack* TSession::get_track(qint64 id) const
{
	if (m_masterOutBusTrack && m_masterOutBusTrack->get_id() == id) {
		return m_masterOutBusTrack;
	}

	return m_tracks.value(id);
}


QList<TBusTrack*> TSession::get_bus_tracks() const
{
	return m_busTracks;
}

TSnapList* TSession::get_snap_list() const
{
	if (m_parentSession) {
		return m_parentSession->get_snap_list();
	}
	return m_snaplist;
}

TLocation* TSession::get_work_snap() const
{
	if (m_parentSession) {
		return m_parentSession->get_work_snap();
	}

	return m_workSnap;
}

TTimeLineRuler* TSession::get_timeline_ruler() const
{
	if (m_parentSession) {
        return m_parentSession->get_timeline_ruler();
	}

    return m_timeLineRuler;
}

TTimeRef TSession::get_work_location() const
{
	if (m_parentSession) {
		return m_parentSession->get_work_location();
	}
	return m_workLocation;
}

TTimeRef TSession::get_last_location() const
{
	if (m_parentSession) {
		return m_parentSession->get_last_location();
	}

	PERROR("TSession::get_last_location(): unsupported configuration, this function needs a parentSession to work!");

	return TTimeRef();
}

TTimeRef TSession::get_transport_location() const
{
	if (m_parentSession) {
		return m_parentSession->get_transport_location();
	}

	return m_transportLocation;
}

qreal TSession::get_hzoom() const
{
	if (m_parentSession) {
		return m_parentSession->get_hzoom();
	}
	int highbit;
    return TraversoDAW::Utils::nearest_power_of_two(ulong(m_hzoom), highbit);
}

QPoint TSession::get_scrollbar_xy()
{
	QPoint point;

	if (m_parentSession) {
		point.setX(m_parentSession->get_scrollbar_xy().x());
	} else {
        point.setX(m_scrollBarXValue);
	}

    point.setY(m_scrollBarYValue);

	return point;
}

bool TSession::is_transport_rolling() const
{
	if (m_parentSession) {
		return m_parentSession->is_transport_rolling();
	}
    return get_transport_rolling_state();
}

bool TSession::is_child_session() const
{
    if (is_project_session()) {
        return false;
    }

	if (!m_parentSession) {
		return false;
	}

	return true;
}

void TSession::set_hzoom( qreal hzoom )
{
	if (m_parentSession) {
		m_parentSession->set_hzoom(hzoom);
		m_hzoom = m_parentSession->m_hzoom;
		return;
	}

	if (hzoom > TPeak::max_zoom_value()) {
		hzoom = TPeak::max_zoom_value();
	}

	if (hzoom < 1.0) {
		hzoom = 1.0;
	}

	qreal old_hzoom = get_hzoom();
	m_hzoom = hzoom;

    if (!TraversoDAW::Float::compare(old_hzoom, get_hzoom())) {
		emit hzoomChanged();
	}
}

void TSession::scale_hzoom(qreal scale)
{
	this->set_hzoom(m_hzoom * scale);
}

void TSession::set_work_at(const TTimeRef &location, bool isFolder)
{
	if (m_parentSession) {
        m_parentSession->set_work_at(location, isFolder);
    }
}

void TSession::set_transport_location(TTimeRef location)
{
	if (m_parentSession) {
		m_parentSession->set_transport_location(location);
	}
}

void TSession::set_temp_follow_state(bool state)
{
	emit tempFollowChanged(state);
}

void TSession::set_scrollbar_x(int x)
{
	// this session mirrors a parent session
	// when transport rolls, this sessions playhead will _also_ try to
	// update the scrollbars position, but it's already taken care of by
	// the parent session. So do nothing here.
	// FIXME: is this a real fix? Should it be managed in PlayHead (Cursors.cpp) ??
	if (m_parentSession && m_parentSession->is_transport_rolling()) {
		return;
	}

	if (m_parentSession) {
		return m_parentSession->set_scrollbar_x(x);
	}

    m_scrollBarXValue = x;

	emit horizontalScrollBarValueChanged();
}

void TSession::set_scrollbar_y(int y)
{
    m_scrollBarYValue = y;

	emit verticalScrollBarValueChanged();
}

void TSession::set_name(const QString& name)
 {
	m_name = name;
	emit propertyChanged();
}

TCommand* TSession::toggle_solo()
{
	if (m_parentSession) {
		return m_parentSession->toggle_solo();
	}

	bool hasSolo = false;

    const auto tracks = get_tracks();

    for(TTrack* track : tracks) {
        if (track->is_solo()) {
            hasSolo = true;
        }
	}

    for (TTrack* track : tracks) {
		track->set_solo(!hasSolo);
		track->set_muted_by_solo(false);
	}

    return nullptr;
}

TCommand* TSession::toggle_mute()
{
	if (m_parentSession) {
		return m_parentSession->toggle_mute();
	}

	bool hasMute = false;
    for(TAudioTrack* track : m_audioTracks) {
        if (track->is_muted()) {
            hasMute = true;
        }
	}

    for(TAudioTrack* track : m_audioTracks) {
		track->set_muted(!hasMute);
	}

    return nullptr;
}

TCommand* TSession::toggle_arm()
{
	if (m_parentSession) {
		return m_parentSession->toggle_arm();
	}

	bool hasArmed = false;
	foreach(TAudioTrack* track, m_audioTracks) {
		if (track->armed()) hasArmed = true;
	}

	foreach(TAudioTrack* track, m_audioTracks) {
		if (hasArmed) {
			track->disarm();
		} else {
			track->arm();
		}
	}

    return nullptr;
}


TCommand* TSession::start_transport()
{
	if (m_parentSession) {
		return m_parentSession->start_transport();
	}
    return nullptr;
}

TCommand* TSession::add_track(TTrack* track, bool historable)
{
    if (is_child_session()) {
        set_track_height(track->get_id(), m_parentSession->get_track_height(track->get_id()));
        private_track_added(track);
        return nullptr;
    }

    return new TAddRemoveCommand(
        this,                                                 // 1. parent (TContextItem*)
        track,                                                // 2. item (TContextItem* via TTrack)
        historable,                                           // 3. bool historable
        this,                                                 // 4. TSession* sheet context (self)
        [this, track]() { private_add_track(track); },       // 5. doMethod closure
        [this, track]() { emit privateTrackAdded(track); },   // 6. doSignal closure
        [this, track]() { private_remove_track(track); },    // 7. undoMethod closure
        [this, track]() { emit privateTrackRemoved(track); }, // 8. undoSignal closure
        tr("Added %1: %2").arg(track->metaObject()->className(), track->get_name()) // 9. description
        );
}

TCommand* TSession::remove_track(TTrack* track, bool historable)
{
    if (m_parentSession) {
        private_track_removed(track);
        return nullptr;
    }

    // Bounce Tracks for now are hard coded, do not remove if the user asks
    if (track->get_type() == TTrack::BOUNCE) {
        return nullptr;
    }

    return new TAddRemoveCommand(
        this,
        track,
        historable,
        this,
        [this, track]() { private_remove_track(track); },
        [this, track]() { emit privateTrackRemoved(track); },
        [this, track]() { private_add_track(track); },
        [this, track]() { emit privateTrackAdded(track); },
        tr("Removed %1: %2").arg(track->metaObject()->className(), track->get_name())
        );
}

void TSession::private_add_track(TTrack* track)
{
	switch (track->get_type()) {
    case TTrack::AUDIO:
        m_rtAudioTracks.append(qobject_cast<TAudioTrack*>(track));
		break;
    case TTrack::BUS:
        m_rtBusTracks.append(qobject_cast<TBusTrack*>(track));
		break;
    case TTrack::BOUNCE:
        // TODO?
        break;
    default:
        qFatal("TSession::private_add_track() Unknown Track type, this is a programming error!");

	}
}

void TSession::private_remove_track(TTrack* track)
{
	switch (track->get_type()) {
    case TTrack::AUDIO:
        m_rtAudioTracks.remove(qobject_cast<TAudioTrack*>(track));
		break;
    case TTrack::BUS:
        m_rtBusTracks.remove(qobject_cast<TBusTrack*>(track));
		break;
    case TTrack::BOUNCE:
        //TODO?
        break;
	default:
        qFatal("TSession::private_remove_track() Unknown Track type, this is a programming error!");
	}
}

void TSession::private_track_added(TTrack *track)
{
	switch(track->get_type()) {
    case TTrack::AUDIO:
        m_audioTracks.append(qobject_cast<TAudioTrack*>(track));
		break;
    case TTrack::BUS:
        m_busTracks.append(qobject_cast<TBusTrack*>(track));
		break;
	default:
        qFatal("TSession::private_track_added() Unknown Track type, this is a programming error!");
	}

	m_tracks.insert(track->get_id(), track);

	if ( (!is_child_session()) && (audiodevice().get_driver_type() == "Jack")) {
		track->connect_to_jack(true, true);
	}

	emit trackAdded(track);
}

void TSession::private_track_removed(TTrack *track)
{
	switch(track->get_type()) {
    case TTrack::AUDIO:
        m_audioTracks.removeAll(qobject_cast<TAudioTrack*>(track));
		break;
    case TTrack::BUS:
        m_busTracks.removeAll(qobject_cast<TBusTrack*>(track));
		break;
	default:
        qFatal("TSession::private_track_removed() Unknown Track type, this is a programming error!");
	}

	m_tracks.remove(track->get_id());

	if ( (!is_child_session()) && (audiodevice().get_driver_type() == "Jack")) {
		track->disconnect_from_jack(true, true);
	}

	emit trackRemoved(track);
}

void TSession::add_child_session(TSession *child)
{
	m_childSessions.append(child);
	emit sessionAdded(child);
}

void TSession::remove_child_session(TSession *child)
{
	m_childSessions.removeAll(child);
	emit sessionRemoved(child);
}

