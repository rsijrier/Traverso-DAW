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

#ifndef T_AUDIOCLIP_H
#define T_AUDIOCLIP_H

#include <QString>
#include <QList>
#include <QDomNode>

#include "TAudioProcessingNode.h"
#include "TProcessCallBackData.h"
#include "TRealTimeLinkedList.h"
#include "TTimeLineEntity.h"
#include "TTimeRef.h"

class TSheet;
class TBufferedAudioStreamReader;
class TBufferedAudioStreamWriter;
class TAudioTrack;
class TPeak;
class AudioBus;
class TAudioPluginChain;
class TFadeCurve;

class TAudioClip : public TAudioProcessingNode, public TTimeLineEntity
{
    Q_OBJECT

public:
    TAudioClip(const QString& name);
    TAudioClip(const QDomNode& node);
    ~TAudioClip() override;

    enum RecordingStatus {
        NO_RECORDING,
        RECORDING,
        FINISHING_RECORDING
    };

    void set_audio_source(TBufferedAudioStreamReader* source);
    int init_recording();
    int process(TProcessCallBackData &processData);

    void move_to_location(const TTimeRef& location) final;
    unsigned int get_timeline_entity_type() const final { return TTimeLineEntity::Type::TypeAudioClip; }


    void set_fade_in_range(double range);
    void set_fade_out_range(double range);
    void set_track(TAudioTrack* track);
    void set_sheet(TSheet* sheet);
    void reset_fade_in();
    void reset_fade_out();

    void set_selected(bool selected);
    void set_as_moving(bool moving);
    int set_state( const QDomNode& node );

    TAudioClip* create_copy();
    TAudioTrack* get_track() const;
    TSheet* get_sheet() const;
    TPeak* get_peak() const {return m_peak;}
    QDomNode get_state(QDomDocument doc);
    TFadeCurve* get_fade_in();
    TFadeCurve* get_fade_out();
    bool has_fade_in() const {return m_fadeIn != nullptr;}
    bool has_fade_out() const {return m_fadeOut != nullptr;}

    TTimeRef get_source_length() const;
    TTimeRef get_length() const {return m_length;}
    TTimeRef get_source_start_location() const {return m_sourceStartLocation;}
    TTimeRef get_source_end_location() const {return m_sourceEndLocation;}
    TTimeRef extandable_length_right() const;
    TTimeRef extandable_lenght_left() const;

    uint get_channel_count() const;
    uint get_rate() const;
    uint get_bitdepth() const;
    inline qint64 get_readsource_id( ) const {return m_readSourceId;}
    qint64 get_sheet_id() const {return m_sheetId;}
    TBufferedAudioStreamReader* get_readsource() const {return m_readSource;};

    QDomNode get_dom_node() const;

    bool is_take() const;
    bool is_selected();
    bool is_locked() const {return m_isLocked;}
    bool has_sheet() const;
    bool is_readsource_invalid() const {return !m_isReadSourceValid;}

    bool operator<(const TAudioClip &other){
        return this->get_location_start() < other.get_location_start();
    }

    bool is_moving() const {return m_isMoving;}

    int recording_state() const;

    float calculate_normalization_factor(float targetdB = 0.0);

    void removed_from_track();

    TAudioClip* next = nullptr;

private:
    TRealTimeLinkedList<TFadeCurve*> m_fades;
    TSheet*                         m_sheet;
    TAudioTrack*                    m_track;
    TBufferedAudioStreamReader*     m_readSource;
    TBufferedAudioStreamWriter*     m_writer;
    TPeak*                          m_peak;
    TFadeCurve*                     m_fadeIn;
    TFadeCurve*                     m_fadeOut;
    QDomNode                        m_domNode;

    TTimeRef m_sourceEndLocation;
    TTimeRef m_sourceStartLocation;
    TTimeRef m_sourceLength;
    TTimeRef m_length;

    bool            m_isTake;
    bool            m_isLocked;
    bool            m_isReadSourceValid;
    bool            m_isMoving;
    bool            m_syncDuringDrag;
    RecordingStatus m_recordingStatus;

    qint64 m_readSourceId;
    qint64 m_sheetId;

    void create_fade(int fadeType);
    void init();
    void set_source_end_location(const TTimeRef& location);
    void set_source_start_location(const TTimeRef& location);
    void set_track_end_location(const TTimeRef& location);
    void set_sources_active_state();
    void process_capture(TProcessCallBackData &processData);

    friend class TResourcesManager;

signals:
    void lockChanged();
    void fadeAdded(TFadeCurve*);
    void fadeRemoved(TFadeCurve*);
    void recordingFinished(TAudioClip*);
    void edgeMoved(bool);

public slots:
    void finish_recording();
    void finish_write_source();
    void set_left_edge(const TTimeRef &location);
    void set_right_edge(const TTimeRef &location);
    void track_audible_state_changed();
    void toggle_mute();
    void toggle_lock();

    TCommand* lock();

private slots:
    void private_add_fade(TFadeCurve* fade);
    void private_remove_fade(TFadeCurve* fade);
    void update_global_configuration();
    void sync_location_to_stream();
};

#endif
