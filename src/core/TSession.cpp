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

#include "AudioDevice.h"
#include "AddRemove.h"
#include "AudioTrack.h"
#include "TConfig.h"
#include "Peak.h"
#include "Utils.h"
#include "TBusTrack.h"
#include "Sheet.h"
#include "SnapList.h"
#include "TLocation.h"
#include "TTimeLineRuler.h"

#include "Debugger.h"

TSession::TSession(TSession *parentSession)
	: ContextItem()
{
	m_parentSession = nullptr;

	if (!parentSession) {
		m_timeline = new TTimeLineRuler(this);
		m_snaplist = new SnapList(this);
        m_workSnap = new TLocation(this);
		m_workSnap->set_snap_list(m_snaplist);
	} else {
		set_parent_session(parentSession);
	}

	init();
}

void TSession::init()
{
	// TODO seek to old position on project exit ?
	m_workLocation = TTimeRef();
	m_transportLocation = TTimeRef();
    m_scrollBarXValue = m_scrollBarYValue = 0;
	m_hzoom = config().get_property("Sheet", "hzoomLevel", 8192).toInt();
    m_transportRolling.store(false);
	m_isSnapOn=true;
	m_isProjectSession = false;

	connect(this, SIGNAL(privateTrackAdded(Track*)), this, SLOT(private_track_added(Track*)));
	connect(this, SIGNAL(privateTrackRemoved(Track*)), this, SLOT(private_track_removed(Track*)));
}

void TSession::set_parent_session(TSession *parentSession)
{
//        if (m_isProjectSession) {

		if (m_parentSession) {
			disconnect(m_parentSession, SIGNAL(transportStarted()), this, SIGNAL(transportStarted()));
			disconnect(m_parentSession, SIGNAL(transportStopped()), this, SIGNAL(transportStopped()));
            disconnect(m_parentSession, SIGNAL(transportLocationChanged()), this, SIGNAL(transportLocationChanged()));
			disconnect(m_parentSession, SIGNAL(workingPosChanged()), this, SIGNAL(workingPosChanged()));
			disconnect(m_parentSession, SIGNAL(hzoomChanged()), this, SIGNAL(hzoomChanged()));
			disconnect(m_parentSession, SIGNAL(horizontalScrollBarValueChanged()), this, SIGNAL(horizontalScrollBarValueChanged()));
		}
		connect(parentSession, SIGNAL(transportStarted()), this, SIGNAL(transportStarted()));
		connect(parentSession, SIGNAL(transportStopped()), this, SIGNAL(transportStopped()));
        connect(parentSession, SIGNAL(transportLocationChanged()), this, SIGNAL(transportLocationChanged()));
		connect(parentSession, SIGNAL(workingPosChanged()), this, SIGNAL(workingPosChanged()));
		connect(parentSession, SIGNAL(hzoomChanged()), this, SIGNAL(hzoomChanged()));
		connect(parentSession, SIGNAL(horizontalScrollBarValueChanged()), this, SIGNAL(horizontalScrollBarValueChanged()));
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

		Track* track = m_parentSession->get_track(id);
		if (track) {
            // add_track(track) should not return a TCommand object
            // but directly add track and return nullptr, this is by design
            Q_ASSERT(add_track(track) == nullptr);
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

	foreach(Track* track, get_tracks()) {
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
QList<Track*> TSession::get_tracks() const
{
	QList<Track*> list;
    for(AudioTrack* track : m_audioTracks) {
		list.append(track);
	}
    for(TBusTrack* track : m_busTracks) {
		list.append(track);
	}

	return list;
}

Track* TSession::get_track(qint64 id) const
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

SnapList* TSession::get_snap_list() const
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

TTimeLineRuler* TSession::get_timeline() const
{
	if (m_parentSession) {
		return m_parentSession->get_timeline();
	}

	return m_timeline;
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
	return m_hzoom;
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
    return m_transportRolling.load();
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
		return m_parentSession->set_hzoom(hzoom);
	}

	// Traverso <= 0.42.0 doesn't store the real zoom factor, but an
	// index. This currently causes problems as there is no real support
	// (yet) for zoomlevels other then powers of 2, so we force that for now.
	// NOTE: Remove those 2 lines when floating point zoomlevel is implemented!
	int highbit;
    hzoom = nearest_power_of_two(ulong(hzoom), highbit);


	if (hzoom > Peak::max_zoom_value()) {
		hzoom = Peak::max_zoom_value();
	}

	if (hzoom < 1.0) {
		hzoom = 1.0;
	}

    if (qFuzzyCompare(m_hzoom, hzoom)) {
		return;
	}

	m_hzoom = hzoom;

	emit hzoomChanged();
}

void TSession::set_work_at(TTimeRef location, bool isFolder)
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

    for(Track* track : tracks) {
        if (track->is_solo()) {
            hasSolo = true;
        }
	}

    for (Track* track : tracks) {
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
    for(AudioTrack* track : m_audioTracks) {
        if (track->is_muted()) {
            hasMute = true;
        }
	}

    for(AudioTrack* track : m_audioTracks) {
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
	foreach(AudioTrack* track, m_audioTracks) {
		if (track->armed()) hasArmed = true;
	}

	foreach(AudioTrack* track, m_audioTracks) {
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


TCommand* TSession::add_track(Track* track, bool historable)
{
	if (is_child_session()) {
		set_track_height(track->get_id(), m_parentSession->get_track_height(track->get_id()));
		private_track_added(track);
        return nullptr;
	}

	return new AddRemove(this, track, historable, this,
		"private_add_track(Track*)", "privateTrackAdded(Track*)",
		"private_remove_track(Track*)", "privateTrackRemoved(Track*)",
        tr("Added %1: %2").arg(track->metaObject()->className(), track->get_name()));
}


TCommand* TSession::remove_track(Track* track, bool historable)
{
	if (m_parentSession) {
		private_track_removed(track);
        return nullptr;
	}

	return new AddRemove(this, track, historable, this,
		"private_remove_track(Track*)", "privateTrackRemoved(Track*)",
		"private_add_track(Track*)", "privateTrackAdded(Track*)",
        tr("Removed %1: %2").arg(track->metaObject()->className(), track->get_name()));
}

void TSession::private_add_track(Track* track)
{
	switch (track->get_type()) {
	case Track::AUDIOTRACK:
        m_rtAudioTracks.append(qobject_cast<AudioTrack*>(track));
		break;
	case Track::BUS:
        m_rtBusTracks.append(qobject_cast<TBusTrack*>(track));
		break;
	default:
        qFatal("TSession::private_add_track() Unknown Track type, this is a programming error!");

	}
}

void TSession::private_remove_track(Track* track)
{
	switch (track->get_type()) {
	case Track::AUDIOTRACK:
        m_rtAudioTracks.remove(qobject_cast<AudioTrack*>(track));
		break;
	case Track::BUS:
        m_rtBusTracks.remove(qobject_cast<TBusTrack*>(track));
		break;
	default:
        qFatal("TSession::private_remove_track() Unknown Track type, this is a programming error!");
	}
}

void TSession::private_track_added(Track *track)
{
	switch(track->get_type()) {
	case Track::AUDIOTRACK:
        m_audioTracks.append(qobject_cast<AudioTrack*>(track));
		break;
	case Track::BUS:
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

void TSession::private_track_removed(Track *track)
{
	switch(track->get_type()) {
	case Track::AUDIOTRACK:
        m_audioTracks.removeAll(qobject_cast<AudioTrack*>(track));
		break;
	case Track::BUS:
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

