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

#pragma once

#include "TContextItem.h"

#include <QDomNode>
#include <QHash>

#include "TChannelBuffer.h"
#include "TLocation.h"
#include "TRtList.h"
#include "TRtTransportPosition.h"
#include "TTransportPositionEstimator.h"
#include "TTimeRef.h"

class TAudioTrack;
class TBounceTrack;
class TSnapList;
class TWorkCursorLocation;
class TBusTrack;
class TTrack;
class TTimeLineRuler;

class TAudioDevice;
class TSession : public TContextItem
{
	Q_OBJECT

public:
    // A root session (a project, a sheet): the audio device it runs on
    explicit TSession(TAudioDevice& device);
    // A child session: its parent's device
    explicit TSession(TSession* parentSession);
    ~TSession();

    // The audio device this session runs on (Traverso owns it)
    TAudioDevice& audio_device() const { return m_device; }

	QDomNode get_state(QDomDocument doc);
	int set_state( const QDomNode & node );


	qreal get_hzoom() const;
	QPoint get_scrollbar_xy();
    bool is_transport_rolling() const T_REALTIME_SAFE;
	TTimeRef get_work_location() const;
	virtual TTimeRef get_last_location() const;
    TTimeRef get_seek_transport_location() const {return m_seekTransportLocation;}
    // GUI thread. While rolling: the end of the buffer the audio thread
    // published last, see get_published_transport_position()
	virtual TTimeRef get_transport_location() const T_REALTIME_SAFE;
    // A seek is requested and not confirmed by the audio thread yet
    virtual bool is_seeking() const T_REALTIME_SAFE { return false; }
    // The position the audio thread published last (the parent's for a
    // child session). Any thread
    TTransportPosition get_published_transport_position() const;
    // GUI thread. While rolling: the position heard at nowNanoseconds (clock
    // of TRtTransportPosition::now_nanoseconds()), computed once per
    // nowNanoseconds so all callers of one frame agree; otherwise the
    // transport location
    TTimeRef get_heard_transport_location(qint64 nowNanoseconds);
	virtual TSnapList* get_snap_list() const;
	TTrack* get_track(qint64 id) const;
	TTimeLineRuler* get_timeline_ruler() const;
	TSession* get_parent_session() const {return m_parentSession;}
	QString get_name() const {return m_name;}
	int get_track_height(qint64 trackId) const {return m_trackHeights.value(trackId, 150);}

	TBusTrack* get_master_out_bus_track() const;
	virtual QList<TTrack*> get_tracks() const;
	QList<TBusTrack*> get_bus_tracks() const;
	QList<TSession*> get_child_sessions() const {return m_childSessions;}
	TLocation* get_work_snap() const;
	virtual bool is_snap_on() const	{return m_isSnapOn;}


	void set_hzoom(qreal hzoom);
    // The zoom limits, in hzoom units (frames per pixel at 44100 Hz)
    virtual qreal minimum_hzoom() const;
    virtual qreal maximum_hzoom() const;
	void scale_hzoom(qreal scale);
    virtual void set_work_at(const TTimeRef &location, bool isFolder=false);
	void set_scrollbar_xy(int x, int y);
	void set_scrollbar_x(int x);
	void set_scrollbar_y(int y);
	void set_parent_session(TSession* parentSession);
    void set_is_project_session(bool isProjectSession) {m_isProjectSession = isProjectSession;}
    bool is_project_session() const {return m_isProjectSession;}
	bool is_child_session() const;
	void set_name(const QString& name);
	void set_track_height(qint64 trackId, int height) {m_trackHeights.insert(trackId, height);}

    TCommand* add_track(TTrack* track, bool historable=true);
    TCommand* remove_track(TTrack* track, bool historable=true);

	void add_child_session(TSession* child);
	void remove_child_session(TSession* child);

    TChannelBuffer& get_curve_buffer() {
        return m_curveProcessBuffer;
    }

protected:
    // First: the members below are made with it
    TAudioDevice& m_device;
	TSession*               m_parentSession;
	QList<TSession*>        m_childSessions;
    TRtList<TAudioTrack>    m_audioTracks;
    TRtList<TBusTrack>      m_busTracks;
	QHash<qint64, TTrack* >	m_tracks;
    TBusTrack*              m_masterOutBusTrack;
    TBounceTrack*           m_bounceTrack;
    QHash<qint64, int>      m_trackHeights;

    TSnapList*           m_snaplist;
    TLocation*          m_workSnap{nullptr};
    TTimeLineRuler*     m_timeLineRuler;
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

    TRealTimeChannelBuffer     	m_curveProcessBuffer;

    bool get_transport_rolling_state() const {
        return m_transportRolling.load();
    }
    void set_transport_rolling_state(bool rolling) {
        m_transportRolling.store(rolling);
    }



private:
	friend class TTimeLineRuler;

public slots:
	void set_temp_follow_state(bool state);
	virtual void set_transport_location(TTimeRef location);

	TCommand* toggle_solo();
	TCommand* toggle_mute();
	TCommand* toggle_arm();
	virtual TCommand* start_transport();

protected:
	void private_add_track(TTrack* track, TRtTransaction& transaction);
	void private_remove_track(TTrack* track, TRtTransaction& transaction);

    // Audio thread, once per processed buffer
    void publish_transport_position(const TTransportPosition& position) {m_rtTransportPosition.publish(position);}

private:
    TSession(TAudioDevice& device, TSession* parentSession);
    TRtTransportPosition        m_rtTransportPosition;
    // GUI thread
    TTransportPositionEstimator m_heardPositionEstimator;
    qint64                      m_heardPositionNanoseconds{-1};
    TTimeRef                    m_heardPosition;


signals:
	void trackRemoved(TTrack* );
	void trackAdded(TTrack* );
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

