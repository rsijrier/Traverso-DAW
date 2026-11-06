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

#include <QString>
#include <QList>
#include <QDomDocument>
#include <QDomNode>

#include "TAudioProcessingNode.h"
#include "TContextItem.h"
#include "TObjectIdTags.h"
#include "TProcessContext.h"
#include "TRtList.h"
#include "TSplitableTimeLineEntity.h"
#include "TTimeRef.h"

class TSheet;
class TBufferedAudioStreamReader;
class TBufferedAudioStreamWriter;
class TAudioTrack;
class TWaveform;
class AudioBus;
class TAudioPluginChain;
class TFadeCurve;
class TRtTransaction;

class TAudioClip : public TContextItem, public TSplitableTimeLineEntity
{
    Q_OBJECT
    // For QML: clip.audio
    Q_PROPERTY(TAudioProcessingNode* audio READ audio CONSTANT)

public:
    TAudioClip(const QString& name);
    TAudioClip(const QDomNode& node);
    ~TAudioClip() override;

    // Its audio: gain, mute, pan and the plugin chain. A clip has a
    // processing node, it is none (T95)
    TAudioProcessingNode* audio() { return &m_audio; }

    QString get_name() const { return m_name; }
    void set_name(const QString& name);

    using IdTag = AudioClipTag;
    // The id, given by TObjectFactory when it made or loaded the clip; a copy
    // is a new clip with its own (doc/design/object-ids.md)
    AudioClipId object_id() const
    {
        Q_ASSERT(m_objectId.is_valid());
        return m_objectId;
    }
    // Its owner: the track it is on while it is in use, nullptr when on none. A
    // removed clip is on none, also while it keeps its track for undo
    QObject* id_owner() const;

    enum RecordingStatus {
        NO_RECORDING,
        RECORDING,
        FINISHING_RECORDING
    };

    void set_audio_source(TBufferedAudioStreamReader* source);
    int init_recording();
    int process(TProcessContext &context) T_REALTIME_SAFE;

    void move_to_location(const TTimeRef& location) final;
    unsigned int get_timeline_entity_type() const final { return TTimeLineEntity::Type::TypeAudioClip; }

    bool validate_split_boundary(const TTimeRef& splitLocation) const final;
    TSplitMutationResult calculate_split_mutation(const TTimeRef& splitLocation) final;


    // Creates the fade if the clip has none yet
    void set_fade_in_length(const TTimeRef& length);
    void set_fade_out_length(const TTimeRef& length);
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
    TWaveform* get_waveform() const {return m_waveform;}
    QDomNode get_state(QDomDocument doc);
    // Create the fade if the clip has none yet, for user actions that give the
    // clip a fade
    TFadeCurve* get_fade_in();
    TFadeCurve* get_fade_out();
    // nullptr if the clip has no fade
    TFadeCurve* get_existing_fade_in() const {return m_fadeIn;}
    TFadeCurve* get_existing_fade_out() const {return m_fadeOut;}
    bool has_fade_in() const {return m_fadeIn != nullptr;}
    bool has_fade_out() const {return m_fadeOut != nullptr;}

    TTimeRef get_source_length() const;
    TTimeRef get_length() const {return m_length;}
    TTimeRef get_source_start_location() const {return m_sourceStartLocation;}
    TTimeRef get_source_end_location() const {return m_sourceEndLocation;}
    TTimeRef extandable_length_right() const;
    TTimeRef extandable_lenght_left() const;

    uint get_channel_count() const T_REALTIME_SAFE;
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
    // A finished take whose writer put no frames in its file
    bool is_empty_take() const {return m_emptyTake;}

    bool operator<(const TAudioClip &other){
        return this->get_location_start() < other.get_location_start();
    }

    bool is_moving() const {return m_isMoving;}

    int recording_state() const T_REALTIME_SAFE;

    float calculate_normalization_factor(float targetdB = 0.0);

    void removed_from_track();

private:
    friend class TObjectFactory;
    AudioClipId m_objectId;
    TAudioProcessingNode            m_audio;
    QString                         m_name;
    TRtList<TFadeCurve>             m_fades;
    TSheet*                         m_sheet;
    TAudioTrack*                    m_track;
    TBufferedAudioStreamReader*     m_readSource;
    TBufferedAudioStreamWriter*     m_writer;
    TWaveform*                      m_waveform{nullptr};
    TFadeCurve*                     m_fadeIn;
    TFadeCurve*                     m_fadeOut;
    // The clip's state as it was loaded, set or saved when the clip is first
    // used; in a document of its own, as the project file's document is gone
    // after the load. The document is declared first, so it goes last
    QDomDocument                    m_domDocument;
    QDomNode                        m_domNode;

    TTimeRef m_sourceEndLocation;
    TTimeRef m_sourceStartLocation;
    // While recording: the frames the writer drops before what was heard
    // (TRecordingLatency), the frames captured so far, and their rate (the
    // device's, which the source's rate need not be until the file is read)
    qint64 m_captureCompensation{0};
    qint64 m_capturedFrames{0};
    uint m_captureRate{0};
    TTimeRef m_sourceLength;
    TTimeRef m_length;

    bool            m_isTake;
    bool            m_emptyTake{false};
    bool            m_isLocked;
    bool            m_isReadSourceValid{false};
    bool            m_isMoving;
    bool            m_syncDuringDrag;
    RecordingStatus m_recordingStatus;

    qint64 m_readSourceId;
    qint64 m_sheetId;

    TFadeCurve* create_fade(int fadeType, TRtTransaction& transaction);
    void init();
    void set_source_end_location(const TTimeRef& location);
    void set_source_start_location(const TTimeRef& location);
    void set_track_end_location(const TTimeRef& location);
    void set_sources_active_state();
    void process_capture(TProcessContext &context) T_REALTIME_SAFE;

    friend class TResourcesManager;

signals:
    // The clip's state: its name, or its gain (the node's gainChanged)
    void stateChanged();
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
    void toggle_lock();

    TCommand* lock();

private slots:
    void add_fade(TFadeCurve* fade, TRtTransaction& transaction);
    void update_global_configuration();
    void sync_location_to_stream();
};

