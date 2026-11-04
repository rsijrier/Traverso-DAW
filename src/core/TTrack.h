/*
Copyright (C) 2005-2024 Remon Sijrier

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

#include "TAudioProcessingNode.h"
#include "TObjectIdTags.h"
#include "TRtList.h"
#include "TRtValue.h"
#include "TTypes.h"

#include <QDomDocument>

class TSend;
class TVUMonitor;
class TProject;
class TRtTransaction;

class TTrack : public TAudioProcessingNode
{
    Q_OBJECT

public:
    TTrack (TSession* session=0);
    virtual ~TTrack ();

    // The id, given by TObjectFactory when it made or loaded the track
    // (doc/design/object-ids.md)
    TrackId object_id() const
    {
        Q_ASSERT(m_objectId.is_valid());
        return m_objectId;
    }
    // Its owner: the song or the project it was made in
    QObject* id_owner() const;

    enum TRACKTYPE{
        AUDIO = 0,
        BUS = 1,
        BOUNCE = 2
    };

    static constexpr int INITIAL_HEIGHT = 90;

    void get_state(QDomDocument& doc, QDomElement& element, bool istemplate=false);
    int get_sort_index() const;
    QList<TVUMonitor*> get_vumonitors() const {return m_vumonitors;}

    void set_muted_by_solo(bool muted);
    void set_solo(bool solo);
    void toggle_show_gain_automation_curve();
    void set_sort_index(int index);
    int set_state( const QDomNode& node );
    virtual void set_name(const QString& name);
    void set_channel_count(int count);
    int get_channel_count() const {return m_channelCount;}
    int get_type() const {return m_type;}

    bool is_muted_by_solo();
    bool is_solo();
    bool presend_on() const {return m_preSendOn;}
    bool show_track_volume_automation() const {return m_showTrackVolumeAutomation;}

    bool operator()(TTrack* left, TTrack* right) {
        return left->get_sort_index() < right->get_sort_index();
    }

    void add_input_bus(const QString& name);
    void add_post_send(qint64 busId);
    void add_post_send(AudioBus* bus);
    void add_pre_send(qint64 busId);
    void remove_post_sends(QList<qint64> sendIds);
    void remove_all_post_sends();
    void remove_post_send(TSend* send);
    void remove_pre_sends(QList<qint64> sendIds);
    void add_input_bus(qint64 busId);

    bool connect_to_jack(bool inports, bool outports);
    bool disconnect_from_jack(bool inports, bool outports);

    AudioBus* get_process_bus() const {return m_processBus;}
    // GUI thread
    AudioBus* get_input_bus() const {return m_inputBus;}
    // Audio thread: the input bus the last applied transaction set, may be nullptr
    AudioBus* rt_input_bus() const {return *m_rtInputBus.rt_snapshot();}

    QString get_bus_in_name() const {return m_busInName;}

    QList<TSend*> get_post_sends() const;
    QList<TSend*> get_pre_sends() const;
    TSend* get_send(qint64 sendId);
    bool has_post_send(qint64 busId) const;
    // True when a send from this track to the bus busId would close a
    // feedback loop (T64): such a send is refused
    bool send_closes_loop(qint64 busId);
    virtual void add_input_bus(AudioBus* bus);


protected:
    QList<TVUMonitor*>  m_vumonitors;
    int             m_sortIndex;
    int             m_type{};
    int             m_channelCount;
    bool            m_mutedBySolo;
    bool            m_isSolo;
    bool		m_showTrackVolumeAutomation;
    bool		m_preSendOn;

    TRtList<TSend>   m_postSends;
    TRtList<TSend>   m_preSends;

    // The GUI thread's value and the audio thread's view of it, changed
    // together in set_input_bus()
    AudioBus*            m_inputBus{nullptr};
    TRtValue<AudioBus*>  m_rtInputBus;
    QString              m_busInName;

    void process_post_sends(nframes_t nframes) T_REALTIME_SAFE;
    void process_pre_sends(nframes_t nframes) T_REALTIME_SAFE;
    void set_input_bus(AudioBus* bus, TRtTransaction& transaction);

private:
    friend class TObjectFactory;
    TrackId m_objectId;

    void process_send(TSend* send, nframes_t nframes);
    void private_add_post_send(TSend* postSend, TRtTransaction& transaction);
    void private_add_pre_send(TSend* preSend, TRtTransaction& transaction);
    void private_remove_post_send(TSend* postSend, TRtTransaction& transaction);
    void private_remove_pre_send(TSend* preSend, TRtTransaction& transaction);

public slots:
    TCommand* solo();
    TCommand* toggle_presend();

signals:
    void soloChanged(bool isSolo);
    void preSendChanged(bool preSendOn);
    void automationVisibilityChanged();
    void routingConfigurationChanged();
};

