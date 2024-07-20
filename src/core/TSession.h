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

#ifndef TSESSION_H
#define TSESSION_H

#include "ContextItem.h"

#include <QDomNode>
#include <QHash>

#include "TRealTimeLinkedList.h"
#include "defines.h"
#include "TTimeRef.h"

class AudioTrack;
class SnapList;
class TLocation;
class TBusTrack;
class Track;
class TTimeLineRuler;

class TSession : public ContextItem
{
	Q_OBJECT

public:
    TSession(TSession* parentSession = nullptr);

	QDomNode get_state(QDomDocument doc);
	int set_state( const QDomNode & node );


	qreal get_hzoom() const;
	QPoint get_scrollbar_xy();
    bool is_transport_rolling() const;
	TTimeRef get_work_location() const;
	virtual TTimeRef get_last_location() const;
    TTimeRef get_seek_transport_location() const {return m_seekTransportLocation;}
	virtual TTimeRef get_transport_location() const;
	virtual SnapList* get_snap_list() const;
	Track* get_track(qint64 id) const;
	TTimeLineRuler* get_timeline() const;
	TSession* get_parent_session() const {return m_parentSession;}
	QString get_name() const {return m_name;}
	int get_track_height(qint64 trackId) const {return m_trackHeights.value(trackId, 150);}

	TBusTrack* get_master_out_bus_track() const;
	virtual QList<Track*> get_tracks() const;
	QList<TBusTrack*> get_bus_tracks() const;
	QList<TSession*> get_child_sessions() const {return m_childSessions;}
	TLocation* get_work_snap() const;
	virtual bool is_snap_on() const	{return m_isSnapOn;}


	void set_hzoom(qreal hzoom);
    virtual void set_work_at(TTimeRef location, bool isFolder=false);
	void set_scrollbar_xy(int x, int y);
	void set_scrollbar_x(int x);
	void set_scrollbar_y(int y);
	void set_parent_session(TSession* parentSession);
	void set_is_project_session(bool isProjectSession) {m_isProjectSession = isProjectSession;}
	bool is_project_session() const {return m_isProjectSession;}
	bool is_child_session() const;
	void set_name(const QString& name);
	void set_track_height(qint64 trackId, int height) {m_trackHeights.insert(trackId, height);}

	TCommand* add_track(Track* api, bool historable=true);
	TCommand* remove_track(Track* api, bool historable=true);

	void add_child_session(TSession* child);
	void remove_child_session(TSession* child);

	audio_sample_t* 	mixdown{};
	audio_sample_t*		gainbuffer{};

protected:
	TSession*               m_parentSession;
	QList<TSession*>        m_childSessions;
    TRealTimeLinkedList<AudioTrack*>           m_rtAudioTracks;
    TRealTimeLinkedList<TBusTrack*>           m_rtBusTracks;
	QList<AudioTrack*>      m_audioTracks;
	QList<TBusTrack*>       m_busTracks;
	QHash<qint64, Track* >	m_tracks;
	TBusTrack*              m_masterOutBusTrack{};
	QHash<qint64, int>      m_trackHeights;

    SnapList*           m_snaplist;
    TLocation*       m_workSnap;
    TTimeLineRuler*     m_timeline;
    QString             m_name;

    int                 m_scrollBarXValue{};
    int                 m_scrollBarYValue{};
    qreal               m_hzoom{};
    bool                m_isSnapOn{};
    bool                m_isProjectSession{};

    std::atomic<bool>   m_transportRolling;
    TTimeRef            m_transportLocation;
    TTimeRef            m_workLocation;
    TTimeRef            m_seekTransportLocation;

private:
	friend class TTimeLineRuler;

	void init();


public slots:
	void set_temp_follow_state(bool state);
	virtual void set_transport_location(TTimeRef location);

	TCommand* toggle_solo();
	TCommand* toggle_mute();
	TCommand* toggle_arm();
	virtual TCommand* start_transport();

protected slots:
	void private_add_track(Track* track);
	void private_remove_track(Track* track);
	void private_track_added(Track* track);
	void private_track_removed(Track* track);


signals:
	void privateTrackRemoved(Track*);
	void privateTrackAdded(Track*);
	void trackRemoved(Track* );
	void trackAdded(Track* );
	void sessionAdded(TSession*);
	void sessionRemoved(TSession*);
	void hzoomChanged();
	void tempFollowChanged(bool state);
	void lastFramePositionChanged();
	void transportStarted();
	void transportStopped();
	void workingPosChanged();
    void transportLocationChanged();
	void horizontalScrollBarValueChanged();
	void verticalScrollBarValueChanged();
	void propertyChanged();
};

#endif // TSESSION_H
