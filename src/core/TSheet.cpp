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

#include <QTextStream>
#include <QString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QMap>
#include <QDebug>

#include <commands.h>

#include "TAudioDevice.h"
#include "AudioBus.h"
#include "TAudioDeviceClient.h"
#include "TProjectManager.h"
#include "TInformUser.h"
#include "TSheet.h"
#include "TProject.h"
#include "TAudioTrack.h"
#include "ResampleAudioReader.h"
#include "TAudioClip.h"
#include "TExportSpecification.h"
#include "TDiskIOThread.h"
#include "TBufferedAudioStreamReader.h"
#include "TBufferedAudioStreamWriter.h"
#include "TAudioClipManager.h"
#include "TAudioThreadMessageQueue.h"
#include "TSelectionService.h"
#include "TSnapList.h"
#include "TBusTrack.h"
#include "TConfig.h"
#include "TUndoCommand.h"
#include "Utils.h"
#include "TTimeLineRuler.h"
#include "TTimeLineMarker.h"
#include "TInputEventDispatcher.h"                       
#include "TSend.h"
#include "TAudioPlugin.h"
#include "TAudioPluginChain.h"
#include "TLocation.h"
#include "Debugger.h"


/**	\class TSheet
	\brief The 'work space' (as in WorkSheet) holding the Track 's and the Master Out AudioBus
	
	A TSheet processes each Track, and mixes the result into it's Master Out AudioBus.
	TSheet connects it's Client to the AudioDevice, to become part of audio processing
	chain. The connection is instantiated by Project, who owns the TSheet objects.
 
 
 */


TSheet::TSheet(TProject* project, int numtracks)
    : TSession(nullptr)
    , m_project(project)
{
    PENTERCONS;
    m_name = tr("Sheet %1").arg(project->get_num_sheets() + 1);
    m_artists = tr("No artists name set");

    init();

    TRtTransaction transaction;

    for (int i=1; i <= numtracks; i++) {
        int height = TAudioTrack::INITIAL_HEIGHT;
        QString trackname = QString("Audio Track %1").arg(i);
        TAudioTrack* track = new TAudioTrack(this, trackname, height);
        private_add_track(track, transaction);
    }

    resize_buffers(audiodevice().get_buffer_size());
}

TSheet::TSheet(TProject* project, const QDomNode& node)
    : TSession(nullptr)
    , m_project(project)
{
    PENTERCONS;

    set_id(node.toElement().attribute("id", "0").toLongLong());
    init();
}

TSheet::~TSheet()
{
    PENTERDES;

    delete m_readDiskIO;
    delete m_writeDiskIO;
    delete m_masterOutBusTrack;
    delete m_renderBus;
    delete m_clipRenderBus;
    delete get_history_stack();
    delete m_audiodeviceClient;
}

void TSheet::init()
{
    PENTER2;

    QObject::tr("TSheet");

    set_transport_seeking_state(false);
    set_transport_locate_requested_state(false);
    set_transport_stop_requested_state(false);

    int converter_type = config().get_property("Conversion", "RTResamplingConverterType", TResampleAudioReader::get_default_resample_quality()).toInt();
    m_readDiskIO = new TDiskIOThread();
    m_readDiskIO->set_output_sample_rate(audiodevice().get_sample_rate());
    m_readDiskIO->set_resample_quality(converter_type);

    connect(m_readDiskIO, &TDiskIOThread::seekFinished, this, &TSheet::seek_finished, Qt::QueuedConnection);

    m_writeDiskIO = new TDiskIOThread();

    m_audioClipManager = new TAudioClipManager(this);
    set_related_context_item( m_audioClipManager );
    m_selectionService = new TSelectionService(this);
    create_history_stack();
    m_timeLineRuler->set_history_stack(get_history_stack());

    connect(this, &TSheet::prepareRecording, this, &TSheet::prepare_recording);
    connect(&config(), &TConfig::configChanged, this, &TSheet::config_changed);

    TAudioBusConfiguration busConfig;
    busConfig.name = "Sheet Render Bus";
    busConfig.channelcount = 2;
    busConfig.type = "output";
    busConfig.isInternalBus = true;
    m_renderBus = new AudioBus(busConfig);

    busConfig.name = "Sheet Clip Render Bus";
    m_clipRenderBus = new AudioBus(busConfig);

    m_masterOutBusTrack = new MasterOutSubGroup(this, tr("Sheet Master"));
    m_masterOutBusTrack->set_gain(0.5);

    m_bounceTrack = new TBounceTrack(this, tr("Bounce"), TTrack::INITIAL_HEIGHT);
    // TODO: only on export add and after export remove this post send
    // m_masterOutBusTrack->add_post_send(m_bounceTrack->get_input_bus());

    resize_buffers(audiodevice().get_buffer_size());

    m_resumeTransport = m_readyToRecord = false;

    m_changed = m_isRecording = m_prepareRecording = false;

    m_audiodeviceClient = new TAudioDeviceClient("sheet_" + QByteArray::number(get_id()));
    m_audiodeviceClient->set_process_callback(
        TProcessCallBack::from_method<TSheet, &TSheet::process>(this)
        );

    m_audiodeviceClient->set_transport_control_callback(
        TTransportControlCallBack::from_method<TSheet, &TSheet::transport_control>(this)
        );
}

int TSheet::set_state( const QDomNode & node )
{
    PENTER;

    // All tracks and clips of the sheet reach the audio thread as one change
    TRtTransaction transaction;

    QDomNode propertiesNode = node.firstChildElement("Properties");
    QDomElement e = propertiesNode.toElement();

    m_name = e.attribute( "title", "" );
    m_artists = e.attribute( "artists", "" );
    set_audio_sources_dir(e.attribute("audiosourcesdir", ""));
    qreal zoom = e.attribute("hzoom", "4096").toDouble();
    set_hzoom(zoom);
    m_scrollBarXValue = e.attribute("sbx", "0").toInt();
    m_scrollBarYValue = e.attribute("sby", "0").toInt();

    bool ok;
    m_workLocation = e.attribute( "m_workLocation", "0").toLongLong(&ok);
    TTimeRef transportLocation = TTimeRef(e.attribute( "transportlocation", "0").toLongLong(&ok));

    // Start seeking to the 'old' transport pos
    set_transport_location(transportLocation);
    set_snapping(e.attribute("snapping", "0").toInt());

    // TTimeLineRuler used to be called TimeLine so to keep old projects
    // working and not lose Markers (which are a child node of TimeLine
    // we keep calling it TimeLine (?)
    m_timeLineRuler->set_state(node.firstChildElement("TimeLine"));


    QDomNode masterOutNode = node.firstChildElement("MasterOut");
    // Force the proper name for our Master Bus Track
    m_masterOutBusTrack->set_name("Sheet Master");
    m_masterOutBusTrack->set_state(masterOutNode.firstChildElement());

    QDomNode busTracksNode = node.firstChildElement("BusTracks");
    QDomNode busTrackNode = busTracksNode.firstChild();

    while(!busTrackNode.isNull()) {
        TBusTrack* busTrack = new TBusTrack(this, busTrackNode);
        busTrack->set_state(busTrackNode);
        private_add_track(busTrack, transaction);

        busTrackNode = busTrackNode.nextSibling();
    }

    QDomNode tracksNode = node.firstChildElement("Tracks");
    QDomNode trackNode = tracksNode.firstChild();

    while(!trackNode.isNull()) {
        TAudioTrack* track = new TAudioTrack(this, trackNode);
        track->set_state(trackNode);
        private_add_track(track, transaction);

        trackNode = trackNode.nextSibling();
    }

    m_audioClipManager->set_state(node.firstChildElement("ClipManager"));

    QDomNode workSheetsNode = node.firstChildElement("WorkSheets");
    QDomNode workSheetNode = workSheetsNode.firstChild();

    while(!workSheetNode.isNull()) {
        TSession* childSession = new TSession(this);
        childSession->set_state(workSheetNode);
        add_child_session(childSession);

        workSheetNode = workSheetNode.nextSibling();
    }

    return 1;
}

QDomNode TSheet::get_state(QDomDocument doc, bool istemplate)
{
    QDomElement sheetNode = doc.createElement("Sheet");

    if (! istemplate) {
        sheetNode.setAttribute("id", get_id());
    } else {
        sheetNode.setAttribute("id", TraversoDAW::Utils::create_id());
    }

    QDomElement properties = doc.createElement("Properties");
    properties.setAttribute("title", m_name);
    properties.setAttribute("artists", m_artists);
    properties.setAttribute("audiosourcesdir", m_audioSourcesDir);
    properties.setAttribute("m_workLocation", m_workLocation.universal_frame());
    properties.setAttribute("transportlocation", m_transportLocation.universal_frame());
    properties.setAttribute("hzoom", m_hzoom);
    properties.setAttribute("sbx", m_scrollBarXValue);
    properties.setAttribute("sby", m_scrollBarYValue);
    properties.setAttribute("snapping", m_isSnapOn);
    sheetNode.appendChild(properties);

    sheetNode.appendChild(m_audioClipManager->get_state(doc));

    sheetNode.appendChild(m_timeLineRuler->get_state(doc));

    QDomNode masterOutNode = doc.createElement("MasterOut");
    masterOutNode.appendChild(m_masterOutBusTrack->get_state(doc, istemplate));
    sheetNode.appendChild(masterOutNode);

    QDomNode tracksNode = doc.createElement("Tracks");

    foreach(TAudioTrack* track, m_audioTracks.items()) {
        tracksNode.appendChild(track->get_state(doc, istemplate));
    }

    sheetNode.appendChild(tracksNode);


    QDomNode busTracksNode = doc.createElement("BusTracks");
    foreach(TBusTrack* busTrack, m_busTracks.items()) {
        busTracksNode.appendChild(busTrack->get_state(doc, istemplate));
    }

    sheetNode.appendChild(busTracksNode);

    QDomNode workSheetsNode = doc.createElement("WorkSheets");
    foreach(TSession* session, m_childSessions) {
        workSheetsNode.appendChild(session->get_state(doc));
    }

    sheetNode.appendChild(workSheetsNode);

    return sheetNode;
}

bool TSheet::any_audio_track_armed()
{
    return get_armed_tracks().size() > 0 || m_bounceTrack->armed();
}

// Get the CD export range based on the TimeLineRuler Marker positions
// Returns:
// true on success
// false if either or both of the locations can't be determined
bool TSheet::get_cd_export_range(TTimeRef &startLocation, TTimeRef &endLocation)
{
    // auto markers = m_timeline->get_cdtrack_list(spec);

    // for (int i = 0; i < markers.size()-1; ++i) {
    //         // round down to the start of the CD frame (75th of a sec)
    //         spec->set_export_start_location(TTimeRef::cd_to_timeref(TTimeRef::timeref_to_cd(markers.at(i)->get_when())));
    //         spec->set_export_end_location(TTimeRef::cd_to_timeref(TTimeRef::timeref_to_cd(markers.at(i+1)->get_when())));
    //         spec->name          = m_timeline->format_cdtrack_name(markers.at(i), i+1);

    if (m_timeLineRuler->get_start_location(startLocation)) {
        // round down to the start of the CD frame (75th of a sec)
        startLocation = TTimeRef::cd_to_timeref(TTimeRef::timeref_to_cd(startLocation));
        PMESG("Start marker found at %s", QS_C(TTimeRef::timeref_to_cd(startLocation)));
    } else {
        PMESG("No start marker found");
        return false;
    }

    if (m_timeLineRuler->get_end_location(endLocation)) {
        endLocation = TTimeRef::cd_to_timeref(TTimeRef::timeref_to_cd(endLocation));
        PMESG("End marker found at %s", QS_C(TTimeRef::timeref_to_cd(endLocation)));
    } else {
        PMESG("No end marker found");
        return false;
    }

    return true;
}

bool TSheet::get_export_range(TTimeRef &exportStartLocation, TTimeRef &exportEndLocation)
{
    QList<TAudioTrack*> tracksToExport;
    auto soloTracks = get_solo_tracks();

    if (soloTracks.size() > 0) {
        tracksToExport = soloTracks;
    } else {
        tracksToExport = m_audioTracks.items();
    }

    TTimeRef trackExportStartlocation;
    TTimeRef trackExportEndlocation;
    exportStartLocation = TTimeRef::max_length();
    exportEndLocation = TTimeRef();

    for(TAudioTrack* track : tracksToExport) {
        if(track->get_export_range(trackExportStartlocation, trackExportEndlocation)) {
            exportStartLocation = std::min(trackExportStartlocation, exportStartLocation);
            exportEndLocation = std::max(trackExportEndlocation, exportEndLocation);
        }
    }

    return (exportStartLocation != TTimeRef::max_length() && exportEndLocation != TTimeRef());
}


void TSheet::set_artists(const QString& pArtists)
{
    m_artists = pArtists;
    emit propertyChanged();
}

void TSheet::set_gain(float gain)
{
    gain = Mixer::clamp_gain(gain);

    m_masterOutBusTrack->set_gain(gain);

    emit stateChanged();
}

void TSheet::set_work_at(const TTimeRef &location, const bool isFolder)
{    
    if ((! isFolder) && m_project->sheets_are_track_folder()) {
        // FIXME
        // m_project->set_work_at calls Sheet::set_work_at effectively crasing the program
        return m_project->set_work_at(location, isFolder);
    }

    TTimeRef workLocation = location;

    // catch location < 0
    if (workLocation < TTimeRef()) {
        workLocation = TTimeRef();
    }

    m_workLocation = workLocation;

    if (m_workSnap->is_snappable()) {
        m_snaplist->mark_dirty();
    }

    emit workingPosChanged();
}

void TSheet::set_work_at_for_sheet_as_track_folder(const TTimeRef &location)
{
    set_work_at(location, true);
}

TCommand* TSheet::toggle_snap()
{
    set_snapping( ! m_isSnapOn );
    return nullptr;
}


void TSheet::set_snapping(bool snapping)
{
    m_isSnapOn = snapping;
    emit snapChanged();
}

/******************************** SLOTS *****************************/


void TSheet::solo_track(TTrack *track)
{
    bool wasSolo = track->is_solo();

    track->set_muted_by_solo(!wasSolo);
    track->set_solo(!wasSolo);

    QList<TAudioTrack*> tracks = get_audio_tracks();


    // If the Track was a Bus Track, then also (un) solo all the AudioTracks
    // that have this Bus Track as the output bus.
    if ((track->get_type() == TTrack::BUS) && !(track == m_masterOutBusTrack)) {
        QList<TAudioTrack*> busTrackAudioTracks;
        foreach(TAudioTrack* sgTrack, tracks) {
            QList<TSend*> sends = sgTrack->get_post_sends();
            foreach(TSend* send, sends) {
                if (send->get_bus_id() == track->get_process_bus()->get_id()) {
                    busTrackAudioTracks.append(sgTrack);
                }
            }
        }

        if (wasSolo) {
            foreach(TAudioTrack* sgTrack, busTrackAudioTracks) {
                sgTrack->set_solo(false);
                sgTrack->set_muted_by_solo(false);
            }
        } else {
            foreach(TAudioTrack* sgTrack, busTrackAudioTracks) {
                sgTrack->set_solo(true);
                sgTrack->set_muted_by_solo(true);
            }
        }
    }

    bool hasSolo = false;

    foreach(TTrack* t, tracks) {
        t->set_muted_by_solo(!t->is_solo());
        if (t->is_solo()) {
            hasSolo = true;
        }
    }

    if (!hasSolo) {
        foreach(TTrack* t, tracks) {
            t->set_muted_by_solo(false);
        }
    }
}


//
//  Function called in RealTime AudioThread processing path
//
int TSheet::process(TProcessCallBackData &processData)
{
    // DiskIO always needs a wakeup call in case we're
    // seeking or want to seek
    m_readDiskIO->wakeup();
    // Write DiskIO goes to sleep if we're transport rolling == false
    // however, after recording it needs to run a little longer to finish
    // the write sources export. For now just wake it up all the time
    m_writeDiskIO->wakeup();

    if (transport_locate_requested()) {
        printf("Sheet::process: starting seek\n");
        rt_inititate_seek();
        return 0;
    }

    if (is_seeking()) {
        return 0;
    }

    // If no need for playback/record, return.
    if (!is_transport_rolling()) {
        return 0;
    }

    if (transport_stop_requested()) {
        set_transport_rolling_state(false);
        set_transport_stop_requested_state(false);
        tsmp().post_to_gui_thread(TAudioThreadMessageQueueEvent{
            .rtTask = nullptr,
            .guiCallback = [this]() { emit transportStopped(); }
        });

        return 0;
    }

    int processResult = 0;

    nframes_t nframes = processData.get_nframes_to_process();
    processData.set_ringbuffer_read_bus(m_clipRenderBus);

    // Process all Tracks.
    for(TAudioTrack* track : m_audioTracks.rt_items()) {
        processResult |= track->process(processData);
    }

    // update the transport location
    m_transportLocation.add_frames(nframes, audiodevice().get_sample_rate());
    m_readDiskIO->set_transport_location(m_transportLocation);

    tsmp().post_to_gui_thread(TAudioThreadMessageQueueEvent{
        .rtTask = nullptr,
        .guiCallback = [this]() { emit transportLocationChanged(); }
    });

    if (!processResult) {
        return 0;
    }

    for(TBusTrack* busTrack : m_busTracks.rt_items()) {
        busTrack->process(processData);
    }

    // Mix the result into the AudioDevice "physical" buffers
    m_masterOutBusTrack->process(processData);

    if (m_bounceTrack->armed()) {
        m_bounceTrack->process(processData);
    } else {
        // FIXME: should only be done when arm state changes for BouncTrack
        m_bounceTrack->get_input_bus()->silence_buffers();
    }

    m_readDiskIO->add_processed_audio_thread_frames(processData.get_nframes_to_process());
    m_writeDiskIO->add_processed_audio_thread_frames(processData.get_nframes_to_process());

    return 1;
}

void TSheet::resize_buffers(nframes_t size)
{
    m_curveProcessBuffer.resize(size);

    QList<AudioChannel*> audioChannels;
    audioChannels.append(m_masterOutBusTrack->get_process_bus()->get_channels());
    audioChannels.append(m_bounceTrack->get_process_bus()->get_channels());
    audioChannels.append(m_renderBus->get_channels());
    audioChannels.append(m_clipRenderBus->get_channels());

    for(auto chan : audioChannels) {
        chan->set_buffer_size(size);
    }
}

void TSheet::audiodevice_params_changed()
{
    PENTER;
    resize_buffers(audiodevice().get_buffer_size());

    // The samplerate possibly has been changed, this initiates
    // a seek in DiskIO, which clears the buffers and refills them
    // with the correct resampled audio data!
    // We need to seek to a different position then the current one,
    // else the seek won't happen at all :)
    auto outputRate = audiodevice().get_sample_rate();
    m_readDiskIO->set_output_sample_rate(outputRate);
    set_transport_location(m_transportLocation + TTimeRef(audiodevice().get_buffer_size(), outputRate));
}

TAudioClipManager * TSheet::get_audioclip_manager( ) const
{
    return m_audioClipManager;
}

QList<TTimeLineEntity *> TSheet::get_all_active_timeline_entities() const
{
    QList<TTimeLineEntity*> locations;
    for (auto& clip : m_audioClipManager->get_clip_list()) {
        locations.append(clip);
    }
    for (auto& marker : m_timeLineRuler->get_markers()) {
        locations.append(marker);
    }

    // locations.append(m_workSnap);

    // How to append TrackStart or Time 0 ?

    return locations;
}

QString TSheet::get_audio_sources_dir() const
{
    if (m_audioSourcesDir.isEmpty() || m_audioSourcesDir.isNull()) {
        printf("no audio sources dir set, returning projects one\n");
        return m_project->get_audiosources_dir();
    }

    return m_audioSourcesDir;
}

void TSheet::set_audio_sources_dir(const QString &dir)
{
    if (dir.isEmpty() || dir.isNull()) {
        m_audioSourcesDir = m_project->get_audiosources_dir();
        return;
    }

    // We're having our own audio sources dir, do the usual checks.
    m_audioSourcesDir = dir;



    QDir asDir;

    if (!asDir.exists(m_audioSourcesDir)) {
        printf("creating new audio sources dir: %s\n", dir.toLatin1().data());
        if (!asDir.mkdir(m_audioSourcesDir)) {
            tInformUser().critical(tr("Cannot create dir %1").arg(m_audioSourcesDir));
        }
    }
}

TTimeRef TSheet::get_last_location() const
{
    TTimeRef lastAudio = m_audioClipManager->get_last_location();
    TTimeRef endMarkerLocation = TTimeRef();
    m_timeLineRuler->get_end_location(endMarkerLocation);
    return std::max(lastAudio , endMarkerLocation);
}

TCommand* TSheet::add_track(TTrack* track, bool historable)
{
    track->set_muted_by_solo( get_solo_tracks().size() > 0 );

    return TSession::add_track(track, historable);
}

// Function is only to be called from GUI thread.
TCommand * TSheet::set_recordable()
{
    Q_ASSERT(QThread::currentThread() == this->thread());

    // Do nothing if transport is rolling!
    if (is_transport_rolling()) {
        return nullptr;
    }

    // Transport is not rolling, it's save now to switch
    // recording state to on /off
    if (is_recording()) {
        set_recording(false, false);
    } else {
        if (!any_audio_track_armed()) {
            tInformUser().critical(tr("No Tracks armed for recording!"));
            return nullptr;
        }

        set_recording(true, false);
    }

    return nullptr;
}

// Function is only to be called from GUI thread.
TCommand* TSheet::set_recordable_and_start_transport()
{
    PENTER;
    Q_ASSERT(this->thread() == QThread::currentThread());

    if (!is_recording()) {
        set_recordable();
    }

    start_transport();

    return nullptr;
}

// Function is only to be called from GUI thread.
TCommand* TSheet::start_transport()
{
    PENTER;
    // FIXME: is this really true, currently not so for the export thread
    // Q_ASSERT(QThread::currentThread() == m_threadPointer);

    // Delegate the transport start (or if we are rolling stop)
    // request to the audiodevice. Depending on the driver in use
    // this call will return directly to us (by a call to transport_control),
    // or handled by the driver
    if (is_transport_rolling()) {
        audiodevice().transport_stop(m_audiodeviceClient, m_transportLocation);
    } else {
        audiodevice().transport_start(m_audiodeviceClient);
    }

    return ied().succes();
}

// Function can be called either from the GUI or RT thread.
// So ALL functions called here need to be RT thread save!!
int TSheet::transport_control(TTransportControl *transportControl)
{
    switch(transportControl->get_state())
    {
    case TTransportControl::TransportStopped:
        // printf("Sheet::transport_control: Stopped\n");
        if (transportControl->get_location() != m_transportLocation) {
            initiate_seek_start(transportControl->get_location());
        }
        if (is_transport_rolling()) {
            stop_transport_rolling();
            if (is_recording()) {
                set_recording(false, transportControl->is_realtime());
            }
        }
        return true;

    case TTransportControl::TransportStarting:
        printf("Sheet::transport_control: TransportStarting\n");
        if (transportControl->get_location() != m_transportLocation) {
            initiate_seek_start(transportControl->get_location());
            return false;
        }
        if (is_seeking()) {
            printf("Sheet::transport_control: Tranport Starting: Still seeking");
            return false;
        }
        if (is_recording())
        {
            if (!m_prepareRecording) {
                m_prepareRecording = true;
                // prepare_recording() is only to be called from the GUI thread
                // so we delegate the prepare_recording() function call via a
                // RT thread save signal!
                Q_ASSERT(transportControl->is_realtime());
                Q_ASSERT(this->thread() != QThread::currentThread());
                tsmp().post_to_gui_thread(TAudioThreadMessageQueueEvent{
                    .rtTask = nullptr,
                    .guiCallback = [this]() { emit prepareRecording(); }
                });
                printf("Sheet::transport_control: Transport Starting: posting 'prepareRecording()' signal to TSMP\n");
                return false;
            }
            if (!m_readyToRecord) {
                PMESG("transport starting: still preparing for record");
                return false;
            }
            printf("Sheet::transort_starting: Transport Starting: Ready for recording\n");
        }
        PMESG("tranport starting: seek finished");
        return true;


    case TTransportControl::TransportRolling:
        if (!is_transport_rolling()) {
            // When the transport rolling request came from a non slave
            // driver, we currently can assume it's comming from the GUI
            // thread, and TransportStarting never was called before!
            // So in case we are recording we have to prepare for recording now!
            if ( ! transportControl->is_slave() && is_recording() ) {
                Q_ASSERT(!transportControl->is_realtime());
                prepare_recording();
            }
            start_transport_rolling(transportControl->is_realtime());
        }
        return true;
    }

    return false;
}

// can be called from GUI and RT thread
void TSheet::initiate_seek_start(TTimeRef location)
{
    if (is_seeking()) {
        printf("Seeking is already true, not starting seek again\n");
        return;
    }

    set_transport_seeking_state(true);

    m_seekTransportLocation = location;

    set_transport_locate_requested_state(true);

    PMESG("tranport starting: initiating seek");
}

//
//  Function is ALWAYS called in RealTime AudioThread processing path
//  Be EXTREMELY carefull to not call functions() that have blocking behavior!!
//
void TSheet::rt_inititate_seek()
{
    Q_ASSERT(this->thread() != QThread::currentThread());

    if (is_transport_rolling()) {
        m_resumeTransport = true;
    }

    set_transport_rolling_state(false);
    set_transport_locate_requested_state(false);

    // only sets a boolean flag and the new seek location, save to call
    m_readDiskIO->set_seek_transport_location(m_seekTransportLocation);
}

void TSheet::seek_finished()
{
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "Sheet::seek_finished", "Called from other Thread!");

    m_transportLocation  = m_seekTransportLocation;
    printf("Sheet::seek_finished: Transport Location is now %s (Sheet: %s)\n",
           QS_C(TTimeRef::timeref_to_ms_3(m_transportLocation)),
           QS_C(get_name()));
    set_transport_seeking_state(false);

    if (m_resumeTransport) {
        start_transport_rolling(false);
        m_resumeTransport = false;
    }

    emit transportLocationChanged();
    PMESG2("Sheet :: leaving seek_finished");
}


// RT thread save function
void TSheet::start_transport_rolling(bool realtime)
{
    set_transport_rolling_state(true);

    if (realtime) {
        tsmp().post_to_gui_thread(TAudioThreadMessageQueueEvent{
            .rtTask = nullptr,
            .guiCallback = [this]() { emit transportStarted(); }
        });
    } else {
        emit transportStarted();
    }

    PMESG("transport rolling");
}

// RT thread save function
void TSheet::stop_transport_rolling()
{
    set_transport_stop_requested_state(true);
    PMESG("transport stopped");
}

// RT thread save function
void TSheet::set_recording(bool recording, bool realtime)
{
    m_isRecording = recording;

    if (!m_isRecording) {
        m_readyToRecord = false;
        m_prepareRecording = false;
    }

    if (realtime) {
        tsmp().post_to_gui_thread(TAudioThreadMessageQueueEvent{
            .rtTask = nullptr,
            .guiCallback = [this]() { emit recordingStateChanged(); }
        });
    } else {
        emit recordingStateChanged();
    }
}


// NON RT thread save function, should only be called from GUI thread!!
void TSheet::prepare_recording()
{
    PENTER;
    Q_ASSERT(QThread::currentThread() == this->thread());

    if (!is_recording()) {
        return;
    }

    if (!any_audio_track_armed()) {
        return;
    }

    QList<TAudioTrack*> armedTracks;
    QList<TAudioClip*> newClips;

    if (m_bounceTrack->armed()) {
        armedTracks.append(m_bounceTrack);
    } else {
        armedTracks = get_armed_tracks();
    }

    for(TAudioTrack* track : std::as_const(armedTracks)) {
        TAudioClip* clip = track->init_recording();
        if (!clip) {
            continue;
        }

        // For autosave purposes, we connect the recordingfinished
        // signal to the clip_finished_recording() slot, and add this
        // clip to our recording clip list.
        // At the time the cliplist is empty, we're sure the recording
        // session is finished, at which time an autosave makes sense.
        connect(clip, &TAudioClip::recordingFinished, this, &TSheet::clip_finished_recording);
        m_recordingClips.append(clip);
        newClips.append(clip);
    }

    if (m_recordingClips.size() == 0) {
        tInformUser().warning(tr("Failed to create Bounce recording source"));
        return;
    }

    if (!newClips.isEmpty()) {
        QString description = m_bounceTrack->armed() ? tr("Bouncing")
                                                     : tr("Recording to %n Clip(s)", "", m_recordingClips.size());

        // All new clips, on all armed tracks, in one undo step
        QUndoCommand* command = create_command_mutate_clips(newClips, {}, description);

        if (QUndoStack* undoStack = get_history_stack()) {
            undoStack->push(command);
        } else {
            command->redo();
            delete command;
        }
    }

    m_readyToRecord = true;
}

void TSheet::clip_finished_recording(TAudioClip * clip)
{
    if (!m_recordingClips.removeAll(clip)) {
        //		PERROR("clip %s was not in recording clip list, cannot remove it!", QS_C(clip->get_name()));
    }

    if (m_recordingClips.isEmpty()) {
        // seems we finished recording completely now
        // all clips have set their resulting ReadSource
        // length and whatsoever, let's do an autosave:
        m_project->save(true);
    }
}


void TSheet::set_transport_location(TTimeRef location)
{
    // Q_ASSERT(QThread::currentThread() ==  this->thread());
    if (location < TTimeRef()) {
        // do nothing
        return;
    }

    printf("Sheet::set_transport_location: set transport to: %s\n", QS_C(TTimeRef::timeref_to_ms_3(location)));
    audiodevice().transport_locate(m_audiodeviceClient, location);
}


void TSheet::config_changed()
{
    int quality = config().get_property("Conversion", "RTResamplingConverterType", TResampleAudioReader::get_default_resample_quality()).toInt();
    if (m_readDiskIO->get_resample_quality() != quality) {
        m_readDiskIO->set_resample_quality(quality);
    }
}


QList< TAudioTrack * > TSheet::get_audio_tracks() const
{
    return m_audioTracks.items();
}

TAudioTrack * TSheet::get_audio_track_for_index(int index)
{
    for(TAudioTrack* track : m_audioTracks.items()) {
        if (track->get_sort_index() == index) {
            return track;
        }
    }

    return nullptr;
}

void TSheet::add_audio_source_to_diskio(TBufferedAudioStreamReader *source) const {
    m_readDiskIO->add_buffered_audio_stream(source);
}

void TSheet::remove_audio_source_from_diskio(TBufferedAudioStreamReader *source) const {
    m_readDiskIO->remove_buffered_audio_stream(source);
}

void TSheet::add_audio_source_to_diskio(TBufferedAudioStreamWriter *source) const {
    m_writeDiskIO->add_buffered_audio_stream(source);
}

void TSheet::remove_audio_source_from_diskio(TBufferedAudioStreamWriter *source) const {
    m_writeDiskIO->remove_buffered_audio_stream(source);
}

int TSheet::get_read_diskio_buffers_fill_status()
{
    return m_readDiskIO->get_buffers_fill_status();
}

int TSheet::get_write_diskio_buffers_fill_status()
{
    return m_writeDiskIO->get_buffers_fill_status();
}

bool TSheet::get_read_diskio_cpu_time(float &time)
{
    return m_readDiskIO->get_cpu_time(time);
}

bool TSheet::get_write_diskio_cpu_time(float &time)
{
    return m_writeDiskIO->get_cpu_time(time);
}

QList<TAudioTrack *> TSheet::get_solo_tracks() const
{
    QList<TAudioTrack*> soloTracks;
    for(auto track : m_audioTracks.items()) {
        if (track->is_solo()) {
            soloTracks.append(track);
        }
    }
    return soloTracks;
}

QList<TAudioTrack *> TSheet::get_armed_tracks() const
{
    QList<TAudioTrack*> armedTracks;
    for(auto track : m_audioTracks.items()) {
        if (track->armed()) {
            armedTracks.append(track);
        }
    }
    return armedTracks;
}

QUndoCommand* TSheet::create_command_mutate_clips(const QList<TAudioClip*>& clipsToAdd,
                                                  const QList<TAudioClip*>& clipsToRemove,
                                                  const QString& description)
{
    TAudioClipManager* clipManager = get_audioclip_manager();
    Q_ASSERT(clipManager != nullptr);

    struct TrackMutation {
        QList<TAudioClip*> toAdd;
        QList<TAudioClip*> toRemove;
    };
    QMap<TAudioTrack*, TrackMutation> mutationsPerTrack;

    for (TAudioClip* clip : clipsToRemove) {
        if (clip && clip->get_track()) {
            mutationsPerTrack[clip->get_track()].toRemove.append(clip);
        }
    }

    for (TAudioClip* clip : clipsToAdd) {
        if (clip && clip->get_track()) {
            mutationsPerTrack[clip->get_track()].toAdd.append(clip);
        }
    }

    auto doAction = [mutationsPerTrack, clipManager](TRtTransaction& transaction) {
        for (auto it = mutationsPerTrack.begin(); it != mutationsPerTrack.end(); ++it) {
            for (TAudioClip* oldClip : it.value().toRemove) {
                clipManager->remove_clip(oldClip);
            }
        }

        for (auto it = mutationsPerTrack.begin(); it != mutationsPerTrack.end(); ++it) {
            it.key()->apply_clip_mutation(it.value().toAdd, it.value().toRemove, transaction);
        }

        for (auto it = mutationsPerTrack.begin(); it != mutationsPerTrack.end(); ++it) {
            for (TAudioClip* newClip : it.value().toAdd) {
                clipManager->add_clip(newClip);
            }
        }
    };

    auto undoAction = [mutationsPerTrack, clipManager](TRtTransaction& transaction) {
        for (auto it = mutationsPerTrack.begin(); it != mutationsPerTrack.end(); ++it) {
            for (TAudioClip* newClip : it.value().toAdd) {
                clipManager->remove_clip(newClip);
            }
        }

        for (auto it = mutationsPerTrack.begin(); it != mutationsPerTrack.end(); ++it) {
            it.key()->apply_clip_mutation(it.value().toRemove, it.value().toAdd, transaction);
        }

        for (auto it = mutationsPerTrack.begin(); it != mutationsPerTrack.end(); ++it) {
            for (TAudioClip* oldClip : it.value().toRemove) {
                clipManager->add_clip(oldClip);
            }
        }
    };

    return new TUndoCommand(description, std::move(doAction), std::move(undoAction));
}

QUndoCommand* TSheet::create_command_split_entities(const QList<TSplitableTimeLineEntity*>& entities, const TTimeRef& splitLocation, const QString& description)
{
    QList<TAudioClip*> totalClipsToAdd;
    QList<TAudioClip*> totalClipsToRemove;

    for (TSplitableTimeLineEntity* entity : entities) {
        if (!entity) continue;

        if (!entity->validate_split_boundary(splitLocation)) {
            continue;
        }

        TSplitMutationResult entityResult = entity->calculate_split_mutation(splitLocation);

        if (entity->get_timeline_entity_type() == TTimeLineEntity::Type::TypeAudioClip) {
            for (TTimeLineEntity* e : entityResult.toRemove) {
                totalClipsToRemove.append(dynamic_cast<TAudioClip*>(e));
            }
            for (TTimeLineEntity* e : entityResult.toAdd) {
                totalClipsToAdd.append(dynamic_cast<TAudioClip*>(e));
            }
        }
        // else if (entity->get_timeline_entity_type() == TTimeLineEntity::Type::TypeVideoClip) { ... }
    }

    if (totalClipsToAdd.isEmpty()) {
        return nullptr;
    }

    return create_command_mutate_clips(totalClipsToAdd, totalClipsToRemove, description);
}

//eof
