/*
Copyright (C) 2005-2010 Remon Sijrier

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

#include "TProcessContext.h"
#include "TPeakBuildService.h"
#include "TIOGroup.h"
#include "TObjectIdTags.h"
#include "TSession.h"
#include <QDomNode>
#include "TTypes.h"
#include "TRealTime.h"

#include <memory>
#include <vector>

class TProject;
class TAudioTrack;
class TBufferedAudioStreamReader;
class TBufferedAudioStreamWriter;
class TAudioClip;
class TIOScheduler;
class TAudioClipManager;
class TAudioDeviceClient;
class AudioBus;
class TSnapList;
class TTimeLineRuler;
class TBusTrack;
class TTrack;
class TTransportControl;
class QUndoCommand;
class TTimeLineEntity;
class TSplitableTimeLineEntity;
class TSelectionService;

class TSheet : public TSession
{
    Q_OBJECT

public:
    // Made by TObjectFactory, which gives the id first: the sheet's master and
    // bounce track derive theirs from it
    TSheet(SongId id, TProject* project, int numtracks=0);
    TSheet(SongId id, TProject* project, const QDomNode &node);
    ~TSheet();

    // Get functions
    QDomNode get_state(QDomDocument doc, bool istemplate=false);
    QString get_artists() const {return m_artists;}

    QList<TAudioTrack*> get_audio_tracks() const;
    QList<TAudioTrack*> get_solo_tracks() const;
    QList<TAudioTrack*> get_armed_tracks() const;

    TAudioTrack* get_audio_track_for_index(int index);
    int get_audio_track_count() const {return m_audioTracks.gui_items().size();}

    TProject* get_project() const {return m_project;}

    using IdTag = SongTag;
    // The id, given by TObjectFactory when it made or loaded the song
    // (doc/design/object-ids.md)
    SongId object_id() const
    {
        Q_ASSERT(m_objectId.is_valid());
        return m_objectId;
    }
    // Its owner is the project, which has no id
    QObject* id_owner() const { return nullptr; }
    TBounceTrack* get_bounce_track() const { return m_bounceTrack; }
    // The peak file of an audio source, built in this sheet's DiskIO idle
    // time (TPeakBuildService)
    TPeakBuild request_peak_build(const QString& audioPath, const QString& peakPath, uint rate, int channelCount, qint64 frames);
    // A recording's peaks (level 0 is complete): the rest in the same idle
    // time, before the recording's file waveform asks for them
    void post_process_peaks(std::shared_ptr<TPeakStream> recorded);
    void add_audio_source_to_diskio(TBufferedAudioStreamReader *source) const;
    void remove_audio_source_from_diskio(TBufferedAudioStreamReader *source) const;
    void add_audio_source_to_diskio(TBufferedAudioStreamWriter *source) const;
    void remove_audio_source_from_diskio(TBufferedAudioStreamWriter *source) const;

    int get_read_lowest_buffer_fill();
    int get_write_lowest_buffer_fill();
    bool get_read_step_load(float &time);
    bool get_write_step_load(float &time);

    TAudioClipManager* get_audioclip_manager() const;
    TSelectionService* get_selection_service() const { return m_selectionService; }
    QList<TTimeLineEntity*> get_all_active_timeline_entities() const;
    AudioBus* get_render_bus() const {return m_renderBus;}

    QString get_audio_sources_dir() const;
    TTimeRef get_last_location() const override;

    TCommand* add_track(TTrack* track, bool historable=true);

    // Set functions
    int set_state( const QDomNode & node );

    void set_artists(const QString& pArtistis);
    void set_work_at(const TTimeRef &location, const bool isFolder=false) override;
    void set_work_at_for_sheet_as_track_folder(const TTimeRef& location);
    void set_snapping(bool snap);
    void set_recording(bool recording, bool realtime) T_REALTIME_SAFE;
    void set_audio_sources_dir(const QString& dir);

    int process(TProcessContext &context) T_REALTIME_SAFE;

    // jackd only feature
    int transport_control(TTransportControl* state) T_REALTIME_SAFE;

    bool get_cd_export_range(TTimeRef &startLocation, TTimeRef &endLocation);
    bool get_export_range(TTimeRef &exportStartLocation, TTimeRef &exportEndLocation);

    void solo_track(TTrack* track);
    void create(int tracksToCreate);

    bool any_audio_track_armed();
    bool is_changed() const {return m_changed;}
    bool is_recording() const {return m_isRecording;}
    // Takes of the last recording whose files are still being written
    bool has_unfinished_takes() const {return !m_recordingClips.isEmpty();}
    qreal minimum_hzoom() const override;

    QUndoCommand* create_command_mutate_clips(const QList<TAudioClip*>& clipsToAdd,
                                              const QList<TAudioClip*>& clipsToRemove,
                                              const QString& description);

    QUndoCommand* create_command_split_entities(const QList<TSplitableTimeLineEntity*>& entities, const TTimeRef& splitLocation, const QString& description);

    // The last undo step, if it is a recording session: undone and dropped,
    // its takes discarded (TResourcesManager::discard_takes()). False when the
    // last step is something else or a recording runs.
    bool discard_last_recording();
    // The recording sessions that are undone: discarded, since they can not
    // come back once the project closes
    void discard_undone_recordings();

private:
    QList<TAudioClip*>	m_recordingClips;   // the takes being written
    // The recording session while it runs (doc/design/recording-session.md):
    // all its takes and the steps that put them in their tracks; one undo
    // step when the last take is written
    QList<TAudioClip*> m_sessionTakes;
    std::vector<std::unique_ptr<QUndoCommand>> m_sessionSteps;
    bool m_sessionBounce{false};
    void start_takes();
    void split_takes();
    void push_recording_session();
    void drop_empty_take(TAudioClip* clip);
    TProject*            m_project;
    friend class TObjectFactory;
    SongId               m_objectId;
    TAudioDeviceClient*	m_audiodeviceClient{};
    AudioBus*           m_renderBus{};
    AudioBus*           m_clipRenderBus{};
    TIOScheduler*             m_readScheduler;
    TIOScheduler*             m_writeScheduler;
    // This song in the read scheduler: its transport, seek, whether it plays
    // or records. mutable: the audio thread and the scheduler write it, it is
    // not the sheet's state.
    mutable TIOGroup    m_ioGroup;
    TAudioClipManager*	m_audioClipManager{};
    TSelectionService*  m_selectionService{nullptr};
    QString             m_audioSourcesDir;

    std::atomic<bool>   m_isSeeking;
    std::atomic<bool>   m_transportLocateRequested;
    std::atomic<bool>   m_transportStopRequested;
    // Set when the transport starts rolling, the audio thread takes the run
    // start location in its first cycle of the run
    std::atomic<bool>   m_runStartPending{false};
    TTimeRef            m_rtRunStart;

    inline void set_transport_locate_requested_state(bool transportLocate) {
        m_transportLocateRequested.store(transportLocate);
    }
    inline bool transport_locate_requested() const {
        return m_transportLocateRequested.load();
    }
    inline void set_transport_seeking_state(bool seeking) {
        m_isSeeking.store(seeking);
    }
    inline bool is_seeking() const T_REALTIME_SAFE override {
        return m_isSeeking.load();
    }
    void set_transport_stop_requested_state(bool stop) {
        m_transportStopRequested.store(stop);
    }
    bool transport_stop_requested() const {
        return m_transportStopRequested.load();
    }

    QString 	m_artists;
    bool 		m_changed{};
    bool		m_resumeTransport{};
    bool		m_isRecording;
    bool		m_prepareRecording{};
    bool		m_readyToRecord{};

    void init();

    void rt_inititate_seek() T_REALTIME_SAFE;
    void initiate_seek_start(TTimeRef location) T_REALTIME_SAFE;
    void start_transport_rolling(bool realtime) T_REALTIME_SAFE;
    void stop_transport_rolling() T_REALTIME_SAFE;

    void resize_buffers(nframes_t size);

    friend class TAudioClipManager;

public slots :
    void seek_finished();
    void audiodevice_params_changed();
    void set_gain(float gain);
    void set_transport_location(TTimeRef location) override;

    TCommand* start_transport() override;
    TCommand* set_recordable();
    TCommand* set_recordable_and_start_transport();
    TCommand* toggle_snap();

signals:
    void snapChanged();
    void setCursorAtEdge();
    void recordingStateChanged();
    void prepareRecording();
    void stateChanged();

private slots:
    void prepare_recording();
    void clip_finished_recording(TAudioClip* clip);
    void config_changed();
    void update_idle_limit();
};

