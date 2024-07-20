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

#include "TSession.h"
#include <QDomNode>
#include <QTimer>
#include "TTransportControl.h"
#include "Tsar.h"
#include "defines.h"

class Project;
class AudioTrack;
class AudioSource;
class AudioTrack;
class AudioClip;
class DiskIO;
class AudioClipManager;
class TAudioDeviceClient;
class AudioBus;
class SnapList;
class TTimeLineRuler;
class TBusTrack;
class Track;

class Sheet : public TSession
{
    Q_OBJECT

public:

    Sheet(Project* project, int numtracks=0);
    Sheet(Project* project, const QDomNode &node);
    ~Sheet();

    // Get functions
    QDomNode get_state(QDomDocument doc, bool istemplate=false);

    QString get_artists() const {return m_artists;}

    QList<AudioTrack*> get_audio_tracks() const;
    QList<AudioTrack*> get_solo_tracks() const;
    QList<AudioTrack*> get_armed_tracks() const;

    AudioTrack* get_audio_track_for_index(int index);
    int get_audio_track_count() const {return m_audioTracks.size();}

    Project* get_project() const {return m_project;}
    DiskIO*	get_read_diskio() const {
        return m_readDiskIO;
    }
    DiskIO* get_write_diskio() const {
        return m_writeDiskIO;
    }

    AudioClipManager* get_audioclip_manager() const;

    AudioBus* get_render_bus() const {return m_renderBus;}
    AudioBus* get_clip_render_bus() const {return m_clipRenderBus;}


    QString get_audio_sources_dir() const;

    TTimeRef get_last_location() const;

    TCommand* add_track(Track* track, bool historable=true);

    // Set functions
    int set_state( const QDomNode & node );

    void set_artists(const QString& pArtistis);
    void set_work_at(TTimeRef location, bool isFolder=false);
    void set_work_at_for_sheet_as_track_folder(const TTimeRef& location);
    void set_snapping(bool snap);
    void set_recording(bool recording, bool realtime);
    void set_audio_sources_dir(const QString& dir);

    void skip_to_start();
    void skip_to_end();


    int process(nframes_t nframes);

    // jackd only feature
    int transport_control(TTransportControl* state);

    bool get_cd_export_range(TTimeRef &startLocation, TTimeRef &endLocation);
    bool get_export_range(TTimeRef &exportStartLocation, TTimeRef &exportEndLocation);

    void solo_track(Track* track);
    void create(int tracksToCreate);

    bool any_audio_track_armed();
    bool is_changed() const {return m_changed;}
    bool is_snap_on() const	{return m_isSnapOn;}
    bool is_recording() const {return m_recording;}

    bool operator<(const Sheet& /*right*/) {
        return true;
    }

    Sheet* next = nullptr;

private:
    QList<AudioClip*>	m_recordingClips;
    QTimer              m_skipTimer;
    Project*            m_project;
    TAudioDeviceClient*	m_audiodeviceClient{};
    AudioBus*           m_renderBus{};
    AudioBus*           m_clipRenderBus{};
    DiskIO*             m_readDiskIO;
    DiskIO*             m_writeDiskIO;
    AudioClipManager*	m_acmanager{};
    QList<TTimeRef>		m_xposList;
    QString             m_audioSourcesDir;
    TsarEvent           m_transportStoppedTsarEvent;
    TsarEvent           m_seekStartTsarEvent;
    TsarEvent           m_transportLocationChangedTsarEvent;

    std::atomic<bool>   m_seeking;
    std::atomic<bool>   m_startSeek;
    std::atomic<bool>   m_stopTransport;

    inline void set_start_seek(bool startSeek) {
        m_startSeek.store(startSeek);
    }
    inline bool start_seek() const {
        return m_startSeek.load();
    }
    inline void set_seeking(bool seeking) {
        m_seeking.store(seeking);
    }
    inline bool is_seeking() const {
        return m_seeking.load();
    }

    QString 	m_artists;
    bool 		m_changed{};
    bool		m_resumeTransport{};
    bool		m_recording;
    bool		m_prepareRecording{};
    bool		m_readyToRecord{};

    void init();

    void inititate_seek();
    void initiate_seek_start(TTimeRef location);
    void start_transport_rolling(bool realtime);
    void stop_transport_rolling();
    void update_skip_positions();

    void resize_buffer(nframes_t size);

    friend class AudioClipManager;

public slots :
    void seek_finished();
    void audiodevice_params_changed();
    void set_gain(float gain);
    void set_transport_location(TTimeRef location);


    TCommand* next_skip_pos();
    TCommand* prev_skip_pos();
    TCommand* start_transport();
    TCommand* set_recordable();
    TCommand* set_recordable_and_start_transport();
    TCommand* toggle_snap();

signals:
    void seekStart();
    void snapChanged();
    void setCursorAtEdge();
    void recordingStateChanged();
    void prepareRecording();
    void stateChanged();

private slots:
    void handle_diskio_writebuffer_overrun();
    void handle_diskio_readbuffer_underrun();
    void prepare_recording();
    void clip_finished_recording(AudioClip* clip);
    void config_changed();
};

#endif

//eof
