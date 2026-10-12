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

#ifndef SONG_H
#define SONG_H

#include "TProcessCallBackData.h"
#include "TSession.h"
#include <QDomNode>
#include "defines.h"

class TProject;
class TAudioTrack;
class TBufferedAudioStreamReader;
class TBufferedAudioStreamWriter;
class TAudioTrack;
class TAudioClip;
class TDiskIOThread;
class TAudioClipManager;
class TAudioDeviceClient;
class AudioBus;
class TSnapList;
class TTimeLineRuler;
class TBusTrack;
class TTrack;
class TTransportControl;
class TSheet : public TSession
{
    Q_OBJECT
public:

    TSheet(TProject* project, int numtracks=0);
    TSheet(TProject* project, const QDomNode &node);
    ~TSheet();

    // Get functions
    QDomNode get_state(QDomDocument doc, bool istemplate=false);

    QString get_artists() const {return m_artists;}

    QList<TAudioTrack*> get_audio_tracks() const;
    QList<TAudioTrack*> get_solo_tracks() const;
    QList<TAudioTrack*> get_armed_tracks() const;

    TAudioTrack* get_audio_track_for_index(int index);
    int get_audio_track_count() const {return m_audioTracks.size();}

    TProject* get_project() const {return m_project;}
    void add_audio_source_to_diskio(TBufferedAudioStreamReader *source) const;
    void remove_audio_source_from_diskio(TBufferedAudioStreamReader *source) const;
    void add_audio_source_to_diskio(TBufferedAudioStreamWriter *source) const;
    void remove_audio_source_from_diskio(TBufferedAudioStreamWriter *source) const;
    int get_read_diskio_buffers_fill_status();
    int get_write_diskio_buffers_fill_status();
    bool get_read_diskio_cpu_time(float &time);
    bool get_write_diskio_cpu_time(float &time);
    TAudioClipManager* get_audioclip_manager() const;

    AudioBus* get_render_bus() const {return m_renderBus;}


    QString get_audio_sources_dir() const;

    TTimeRef get_last_location() const;

    TCommand* add_track(TTrack* track, bool historable=true);

    // Set functions
    int set_state( const QDomNode & node );

    void set_artists(const QString& pArtistis);
    void set_work_at(const TTimeRef &location, const bool isFolder=false);
    void set_work_at_for_sheet_as_track_folder(const TTimeRef& location);
    void set_snapping(bool snap);
    void set_recording(bool recording, bool realtime);
    void set_audio_sources_dir(const QString& dir);

    int process(TProcessCallBackData &processData);

    // jackd only feature
    int transport_control(TTransportControl* state);

    bool get_cd_export_range(TTimeRef &startLocation, TTimeRef &endLocation);
    bool get_export_range(TTimeRef &exportStartLocation, TTimeRef &exportEndLocation);

    void solo_track(TTrack* track);
    void create(int tracksToCreate);

    bool any_audio_track_armed();
    bool is_changed() const {return m_changed;}
    bool is_recording() const {return m_isRecording;}

    bool operator<(const TSheet& /*right*/) {
        return true;
    }

    TSheet* next = nullptr;

private:
    QList<TAudioClip*>	m_recordingClips;
    TProject*            m_project;
    TAudioDeviceClient*	m_audiodeviceClient{};
    AudioBus*           m_renderBus{};
    AudioBus*           m_clipRenderBus{};
    TDiskIOThread*             m_readDiskIO;
    TDiskIOThread*             m_writeDiskIO;
    TAudioClipManager*	m_audioClipManager{};
    QString             m_audioSourcesDir;

    std::atomic<bool>   m_isSeeking;
    std::atomic<bool>   m_transportLocateRequested;
    std::atomic<bool>   m_transportStopRequested;

    inline void set_transport_locate_requested_state(bool transportLocate) {
        m_transportLocateRequested.store(transportLocate);
    }
    inline bool transport_locate_requested() const {
        return m_transportLocateRequested.load();
    }
    inline void set_transport_seeking_state(bool seeking) {
        m_isSeeking.store(seeking);
    }
    inline bool is_seeking() const {
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

    void rt_inititate_seek();
    void initiate_seek_start(TTimeRef location);
    void start_transport_rolling(bool realtime);
    void stop_transport_rolling();

    void resize_buffers(nframes_t size);

    friend class TAudioClipManager;
public slots :
    void seek_finished();
    void audiodevice_params_changed();
    void set_gain(float gain);
    void set_transport_location(TTimeRef location);


    TCommand* start_transport();
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
};

#endif

//eof
