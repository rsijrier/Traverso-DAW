/*
Copyright (C) 2005-2008 Remon Sijrier

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

#include <cfloat>
#include <QInputDialog>

#include "ContextItem.h"
#include "ReadSource.h"
#include "AudioClip.h"
#include "AudioSource.h"
#include "WriteSource.h"
#include "Sheet.h"
#include "SnapList.h"
#include "AudioTrack.h"
#include "AudioChannel.h"
#include <AudioBus.h>
#include <AudioDevice.h>
#include "Mixer.h"
#include "DiskIO.h"
#include "TExportSpecification.h"
#include "AudioClipManager.h"
#include "ResourcesManager.h"
#include "Curve.h"
#include "FadeCurve.h"
#include "Tsar.h"
#include "ProjectManager.h"
#include "Peak.h"
#include "ContextPointer.h"
#include "Project.h"
#include "Utils.h"
#include "Information.h"
#include "TConfig.h"
#include "PluginChain.h"
#include "GainEnvelope.h"
#include "TInputEventDispatcher.h"


#include <commands.h>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

/**
 *	\class AudioClip
    \brief Represents (part of) an audiofile.
 */

AudioClip::AudioClip(const QString& name)
{
    PENTERCONS;
    m_name = name;
    m_isMuted=false;
    m_readSourceId = m_sheetId = 0;
    QObject::tr("AudioClip");

    m_sheet = nullptr;
    m_track = nullptr;
    m_readSource = nullptr;
    m_writer = nullptr;
    m_peak = nullptr;
    m_recordingStatus = NO_RECORDING;
    m_isReadSourceValid = m_isMoving = false;
    m_isLocked = config().get_property("AudioClip", "LockByDefault", false).toBool();
    m_isTake = false;
    m_syncDuringDrag = false;
    m_fadeIn = nullptr;
    m_fadeOut = nullptr;
    m_fader->automate_port(0, true);
    m_maxGainAmplification = dB_to_scale_factor(24);
    m_locationItem = new TLocation(this);

    // read in the configuration from the global configuration settings.
    update_global_configuration();

    connect(&config(), SIGNAL(configChanged()), this, SLOT(update_global_configuration()));
}


AudioClip::AudioClip(const QDomNode& node)
    : AudioClip("")
{
    PENTERCONS;

    // It makes sense to set these values at this time allready
    // they are for example used by the ResourcesManager!
    QDomElement e = node.toElement();
    set_id(e.attribute("id", "").toLongLong());
    m_readSourceId = e.attribute("source", "").toLongLong();
    m_sheetId = e.attribute("sheet", "0").toLongLong();
    m_name = e.attribute( "clipname", "" ) ;
    m_isMuted =  e.attribute( "mute", "" ).toInt();
    m_length = TTimeRef(e.attribute( "length", "0" ).toLongLong());
    m_sourceStartLocation = TTimeRef(e.attribute( "sourcestart", "" ).toLongLong());

    m_sourceEndLocation = m_sourceStartLocation + m_length;
    TTimeRef location(e.attribute( "trackstart", "" ).toLongLong());
    m_domNode = node.cloneNode();
    //	init();
    // first init to set variables that are referenced in:
    AudioClip::set_location_start(location);
}

AudioClip::~AudioClip()
{
    PENTERDES;
    if (m_readSource) {
        QMetaObject::invokeMethod(m_sheet->get_read_diskio(), "remove_and_delete_audio_source", Qt::QueuedConnection, qobject_cast<AudioSource*>(m_readSource));
    }

    if (m_peak) {
        m_peak->close();
    }

    delete m_locationItem;
}

void AudioClip::init()
{
}

void AudioClip::update_global_configuration()
{
    m_syncDuringDrag = config().get_property("AudioClip", "SyncDuringDrag", false).toBool();
}

TCommand* AudioClip::toggle_show_gain_automation_curve()
{
    m_track->toggle_show_clip_volume_automation();
    return ied().succes();
}

int AudioClip::set_state(const QDomNode& node)
{
    PENTER;

    Q_ASSERT(m_sheet);

    QDomElement e = node.toElement();

    m_isTake = e.attribute( "take", "").toInt();
    set_gain( e.attribute( "gain", "" ).toFloat() );
    m_isLocked = e.attribute( "locked", "0" ).toInt();
    m_readSourceId = e.attribute("source", "").toLongLong();
    m_sheetId = e.attribute("sheet", "0").toLongLong();
    m_isMuted =  e.attribute( "mute", "" ).toInt();

    bool ok;
    m_sourceStartLocation = TTimeRef(e.attribute( "sourcestart", "" ).toLongLong(&ok));
    m_length = TTimeRef(e.attribute( "length", "0" ).toLongLong(&ok));
    m_sourceEndLocation = m_sourceStartLocation + m_length;

    emit stateChanged();

    QDomElement fadeInNode = node.firstChildElement("FadeIn");
    if (!fadeInNode.isNull()) {
        if (!m_fadeIn) {
            m_fadeIn = new FadeCurve(this, FadeCurve::FadeIn);
            private_add_fade(m_fadeIn);
        }
        m_fadeIn->set_state( fadeInNode );
    }

    QDomElement fadeOutNode = node.firstChildElement("FadeOut");
    if (!fadeOutNode.isNull()) {
        if (!m_fadeOut) {
            m_fadeOut = new FadeCurve(this, FadeCurve::FadeOut);
            private_add_fade(m_fadeOut);
        }
        m_fadeOut->set_state( fadeOutNode );
    }

    QDomNode pluginChainNode = node.firstChildElement("PluginChain");
    if (!pluginChainNode.isNull()) {
        m_pluginChain->set_state(pluginChainNode);
    }

    // Curves rely on our start position, so only set the start location
    // after curves (those created in plugins too!) are inited and having
    // their state set.
    TTimeRef location(e.attribute( "trackstart", "" ).toLongLong(&ok));
    AudioClip::set_location_start(location);

    return 1;
}

QDomNode AudioClip::get_state( QDomDocument doc )
{
    QDomElement node = doc.createElement("Clip");
    node.setAttribute("trackstart", m_locationItem->get_start().universal_frame());
    node.setAttribute("sourcestart", m_sourceStartLocation.universal_frame());
    node.setAttribute("length", m_length.universal_frame());
    node.setAttribute("mute", m_isMuted);
    node.setAttribute("take", m_isTake);
    node.setAttribute("clipname", m_name );
    node.setAttribute("id", get_id() );
    node.setAttribute("sheet", m_sheetId );
    node.setAttribute("locked", m_isLocked);

    node.setAttribute("source", m_readSourceId);

    if (m_fadeIn) {
        node.appendChild(m_fadeIn->get_state(doc));
    }
    if (m_fadeOut) {
        node.appendChild(m_fadeOut->get_state(doc));
    }

    QDomNode pluginChainNode = doc.createElement("PluginChain");
    pluginChainNode.appendChild(m_pluginChain->get_state(doc));
    node.appendChild(pluginChainNode);

    return node;
}

void AudioClip::toggle_mute()
{
    PENTER;
    m_isMuted=!m_isMuted;
    set_sources_active_state();
    emit muteChanged();
}

void AudioClip::toggle_lock()
{
    m_isLocked = !m_isLocked;
    emit lockChanged();
}

void AudioClip::track_audible_state_changed()
{
    set_sources_active_state();
}

void AudioClip::set_sources_active_state()
{
    if (! m_track) {
        return;
    }

    if (! m_readSource) {
        return;
    }

    bool stopSyncDueMove;
    if (m_isMoving && m_syncDuringDrag) {
        stopSyncDueMove = false;
    } else if (m_isMoving && !m_syncDuringDrag){
        stopSyncDueMove = true;
    } else {
        stopSyncDueMove = false;
    }

    if ( m_track->is_muted() || m_track->is_muted_by_solo() || is_muted() || stopSyncDueMove) {
        m_readSource->set_active(false);
    } else {
        m_readSource->set_active(true);
    }

}

void AudioClip::removed_from_track()
{
    m_readSource->set_active(false);
}

void AudioClip::set_left_edge(TTimeRef newLeftLocation)
{
    if (newLeftLocation < TTimeRef()) {
        newLeftLocation = TTimeRef();
    }

    if (newLeftLocation < m_locationItem->get_start()) {

        TTimeRef availableTimeLeft = m_sourceStartLocation;

        TTimeRef movingToLeft = m_locationItem->get_start() - newLeftLocation;

        if (movingToLeft > availableTimeLeft) {
            movingToLeft = availableTimeLeft;
        }

        set_source_start_location( m_sourceStartLocation - movingToLeft );
        AudioClip::set_location_start(m_locationItem->get_start() - movingToLeft);
    } else if (newLeftLocation > m_locationItem->get_start()) {

        TTimeRef availableTimeRight = m_length;

        TTimeRef movingToRight = newLeftLocation - m_locationItem->get_start();

        if (movingToRight > (availableTimeRight - TTimeRef(nframes_t(4), get_rate())) ) {
            movingToRight = (availableTimeRight - TTimeRef(nframes_t(4), get_rate()));
        }

        set_source_start_location( m_sourceStartLocation + movingToRight );
        AudioClip::set_location_start(m_locationItem->get_start() + movingToRight);
    }
}

void AudioClip::set_right_edge(TTimeRef newRightLocation)
{

    if (newRightLocation < TTimeRef()) {
        newRightLocation = TTimeRef();
    }

    if (newRightLocation > m_locationItem->get_end()) {

        TTimeRef availableTimeRight = m_sourceLength - m_sourceEndLocation;

        TTimeRef movingToRight = newRightLocation - m_locationItem->get_end();

        if (movingToRight > availableTimeRight) {
            movingToRight = availableTimeRight;
        }

        set_source_end_location( m_sourceEndLocation + movingToRight );
        set_track_end_location( m_locationItem->get_end() + movingToRight );

    } else if (newRightLocation < m_locationItem->get_end()) {

        TTimeRef availableTimeLeft = m_length;

        TTimeRef movingToLeft = m_locationItem->get_end() - newRightLocation;

        if (movingToLeft > availableTimeLeft - TTimeRef(nframes_t(4), get_rate())) {
            movingToLeft = availableTimeLeft - TTimeRef(nframes_t(4), get_rate());
        }

        set_source_end_location( m_sourceEndLocation - movingToLeft);
        set_track_end_location( m_locationItem->get_end() - movingToLeft );
    }
}

void AudioClip::set_source_start_location(const TTimeRef& location)
{
    Q_ASSERT(m_readSource);

    m_sourceStartLocation = location;
    m_readSource->set_source_start_location(location);
    m_length = m_sourceEndLocation - m_sourceStartLocation;
}

void AudioClip::set_source_end_location(const TTimeRef& location)
{
    m_sourceEndLocation = location;
    m_length = m_sourceEndLocation - m_sourceStartLocation;
}

void AudioClip::set_location_start(const TTimeRef& location)
{
    PENTER2;

    m_locationItem->set_start(location);

    m_fader->get_curve()->set_start_offset(m_locationItem->get_start());

    // set_track_end_location will emit positionChanged(), so we
    // don't emit it in this function to avoid emitting it twice
    // (although it seems more logical to emit it here, there are
    // situations where only set_track_end_location() is called, and
    // then we also want to emit positionChanged())
    set_track_end_location(m_locationItem->get_start() + m_length);
}

void AudioClip::set_track_end_location(const TTimeRef& location)
{
    m_locationItem->set_end(location);

    if ( (!is_moving()) && m_sheet) {
        m_sheet->get_snap_list()->mark_dirty();
        m_track->clip_position_changed(this);
    }

    emit positionChanged();
}

void AudioClip::set_fade_in(double range)
{
    if (!m_fadeIn) {
        create_fade(FadeCurve::FadeIn);
    }
    m_fadeIn->set_range(range);
}

void AudioClip::set_fade_out(double range)
{
    if (!m_fadeOut) {
        create_fade(FadeCurve::FadeOut);
    }
    m_fadeOut->set_range(range);
}

void AudioClip::set_selected(bool /*selected*/)
{
    emit stateChanged();
}

//
//  Function called in RealTime AudioThread processing path
//
int AudioClip::process(const TTimeRef& startLocation, const TTimeRef& endLocation, nframes_t nframes)
{
    // Handle silence clips
    if (get_channel_count() == 0) {
        return 0;
    }

    if (m_recordingStatus == RECORDING) {
        process_capture(nframes);
        return 0;
    }

    if (!m_isReadSourceValid) {
        return -1;
    }

    if (m_isMuted || (get_gain() == 0.0f) ) {
        return 0;
    }

    if ((startLocation >= m_locationItem->get_end()) || (endLocation <= m_locationItem->get_start())) {
        return 0;
    }

    Q_ASSERT(m_sheet);
    Q_ASSERT(m_readSource);

    AudioBus* bus = m_sheet->get_clip_render_bus();

    TTimeRef fileLocation;
    audio_sample_t* mixdown[2];
    nframes_t framesToProcess = nframes;
    nframes_t offset = 0;
    uint outputRate = m_readSource->get_output_rate();
    uint channelcount = get_channel_count();
    Q_ASSERT(bus->get_channel_count() >= channelcount);

    if (startLocation < m_locationItem->get_start()) {
        fileLocation = m_sourceStartLocation;
        offset = TTimeRef::to_frame(m_locationItem->get_start() - startLocation, outputRate);
        framesToProcess -= offset;
        Q_ASSERT(offset < nframes);
        Q_ASSERT(framesToProcess > 0);
    } else {
        fileLocation = (startLocation - m_locationItem->get_start() + m_sourceStartLocation);
    }

    if (m_locationItem->get_end() < endLocation) {
        framesToProcess -= TTimeRef::to_frame(endLocation - m_locationItem->get_end(), outputRate);
        Q_ASSERT(framesToProcess > 0);
    }

    // Read the frames from the ringbuffers
    // FIXME change when audio thread supports realtime/freewheeling
    bool realTime = true;
    nframes_t readFrames = m_readSource->ringbuffer_read(bus, fileLocation, nframes, realTime);


    if (readFrames == 0) {
        bus->silence_buffers(nframes);
        return 0;
    }

    if (readFrames != framesToProcess) {
        std::cout << QString("AudioClip::process(): readFrames %1, framesToProcess %2").arg(readFrames).arg(framesToProcess).toLatin1().data() << &std::endl;
    }

    for(FadeCurve* fade = m_fades.first(); fade != nullptr; fade = fade->next) {
        fade->process(bus, startLocation, endLocation, nframes);
    }

    TTimeRef faderEndLocation = fileLocation + TTimeRef(readFrames, outputRate);    
    for (uint chan=0; chan<bus->get_channel_count(); ++chan) {
        audio_sample_t* buf = bus->get_buffer(chan, framesToProcess);
        mixdown[chan] = buf + offset;
    }

    m_fader->process_gain(mixdown, fileLocation, faderEndLocation, readFrames, channelcount);

    AudioBus* processBus = m_track->get_process_bus();

    // Mixing should be done on the WHOLE buffer, not just part of it
    // so use an unmodified nframes variable
    if (channelcount == 1) {
        Mixer::mix_buffers_no_gain(processBus->get_buffer(0, nframes), bus->get_buffer(0, nframes), nframes);
        Mixer::mix_buffers_no_gain(processBus->get_buffer(1, nframes), bus->get_buffer(0, nframes), nframes);
    } else if (channelcount == 2) {
        Mixer::mix_buffers_no_gain(processBus->get_buffer(0, nframes), bus->get_buffer(0, nframes), nframes);
        Mixer::mix_buffers_no_gain(processBus->get_buffer(1, nframes), bus->get_buffer(1, nframes), nframes);
    }

    return 1;
}

//
//  Function called in RealTime AudioThread processing path
//
void AudioClip::process_capture(nframes_t nframes)
{
    AudioBus* bus = m_track->get_input_bus();

    if (!bus) {
        return;
    }

    bool realTime = false;
    nframes_t written = m_writer->ringbuffer_write(bus, nframes, realTime);

    m_length.add_frames(written, get_rate());

    if (written != nframes) {
        printf("AudioClip::process_capture: couldn't write nframes %d to recording buffer for channel 0, only %d\n", nframes, written);
    }
}

int AudioClip::init_recording()
{
    Q_ASSERT(m_sheet);
    Q_ASSERT(m_track);

    AudioBus* bus = m_track->get_input_bus();
    uint channelcount = bus->get_channel_count();

    if (channelcount == 0) {
        // Can't record from a Bus with no channels!
        return -1;
    }

    m_sourceStartLocation = TTimeRef();
    m_isTake = true;
    m_recordingStatus = RECORDING;

    ReadSource* rs = resources_manager()->create_recording_source(
                m_sheet->get_audio_sources_dir(),
                m_name, channelcount, m_sheet->get_id());

    resources_manager()->set_source_for_clip(this, rs);

    QString sourceid = QString::number(rs->get_id());

    auto spec = new TExportSpecification;

    spec->set_export_dir(m_sheet->get_audio_sources_dir());

    QString recordFormat = config().get_property("Recording", "FileFormat", "wav").toString();
    if (recordFormat == "wavpack") {
        spec->set_writer_type("wavpack");
        QString compression = config().get_property("Recording", "WavpackCompressionType", "fast").toString();
        QString skipwvx = config().get_property("Recording", "WavpackSkipWVX", "false").toString();
        spec->extraFormat["quality"] = compression;
        spec->extraFormat["skip_wvx"] = skipwvx;
    }
    else if (recordFormat == "w64") {
        spec->set_file_format(SF_FORMAT_W64);
    } else {
        spec->set_file_format(SF_FORMAT_WAV);
    }

    spec->set_recording_state(TExportSpecification::RecordingState::RECORDING);
    spec->set_block_size(audiodevice().get_buffer_size());
    spec->set_channel_count(channelcount);
    spec->set_render_buffer(bus->get_buffer(0, audiodevice().get_buffer_size()));
    spec->set_sample_rate(audiodevice().get_sample_rate());
    spec->set_export_start_location(TTimeRef());
    spec->set_export_end_location(TTimeRef());
    spec->set_export_file_name(m_name + "-" + sourceid);

    m_writer = new WriteSource(spec);
    if (m_writer->prepare_export() == -1) {
        delete m_writer;
        m_writer = nullptr;
        delete spec;
        spec = nullptr;
        return -1;
    }
    m_writer->set_process_peaks( true );
    m_writer->set_recording( true );

    QMetaObject::invokeMethod(m_sheet->get_write_diskio(), "add_audio_source", Qt::QueuedConnection, qobject_cast<AudioSource*>(m_writer));

    // Writers exportFinished() signal comes from DiskIO thread, so we have to connect by Qt::QueuedConnection
    // or else we deadlock in DiskIO::do_work()
    connect(m_writer, SIGNAL(exportFinished()), this, SLOT(finish_write_source()), Qt::QueuedConnection);
    connect(m_sheet, SIGNAL(transportStopped()), this, SLOT(finish_recording()));

    return 1;
}

TCommand* AudioClip::mute()
{
    toggle_mute();
    return nullptr;
}

TCommand* AudioClip::lock()
{
    toggle_lock();
    return nullptr;
}

TCommand* AudioClip::reset_fade_in()
{
    if (m_fadeIn) {
        return new FadeRange(this, m_fadeIn, 1.0);
    }
    return nullptr;
}

TCommand* AudioClip::reset_fade_out()
{
    if (m_fadeOut) {
        return new FadeRange(this, m_fadeOut, 1.0);
    }
    return nullptr;
}

TCommand* AudioClip::reset_fade_both()
{
    if (!m_fadeOut && !m_fadeIn) {
        return nullptr;
    }

    CommandGroup* group = new CommandGroup(this, tr("Remove Fades"));

    if (m_fadeIn) {
        group->add_command(reset_fade_in());
    }
    if (m_fadeOut) {
        group->add_command(reset_fade_out());
    }

    return group;
}

AudioClip* AudioClip::create_copy( )
{
    PENTER;
    Q_ASSERT(m_sheet);
    Q_ASSERT(m_track);
    QDomDocument doc("AudioClip");
    QDomNode clipState = get_state(doc);
    auto clip = new AudioClip(m_name);
    clip->set_sheet(m_sheet);
    clip->set_track(m_track);
    clip->set_state(clipState);
    return clip;
}

void AudioClip::set_audio_source(ReadSource* rs)
{
    PENTER;

    if (!rs) {
        m_isReadSourceValid = false;
        return;
    }

    if (rs->get_error() < 0) {
        m_isReadSourceValid = false;
    } else {
        m_isReadSourceValid = true;
    }

    m_readSource = rs;
    m_readSource->set_location(m_locationItem);
    m_readSource->set_source_start_location(get_source_start_location());
    m_readSourceId = rs->get_id();
    m_sourceLength = rs->get_length();

    // If m_length isn't set yet, it means we are importing stuff instead of reloading from project file.
    // it's a bit weak this way, hopefull I'll get up something better in the future.
    // The positioning-length-offset and such stuff is still a bit weak :(
    // NOTE: don't change, audio recording (finish_writesource()) assumes there is checked for length == 0 !!!
    if (m_length == TTimeRef()) {
        m_sourceEndLocation = rs->get_length();
        m_length = m_sourceEndLocation;
    }

    set_sources_active_state();

    if (m_recordingStatus == NO_RECORDING) {
        if (m_peak) {
            m_peak->close();
        }
        if (m_isReadSourceValid) {
            m_peak = new Peak(rs);
        }
    }

    // This will also emit positionChanged() which is more or less what we want.
    set_track_end_location(m_locationItem->get_start() + m_sourceLength - m_sourceStartLocation);
}

void AudioClip::finish_write_source()
{
    PENTER;

    Q_ASSERT(m_readSource);

    if (m_readSource->set_file(m_writer->get_filename()) < 0) {
        PERROR("Setting file for ReadSource failed after finishing recording");
    } else {
        QMetaObject::invokeMethod(m_sheet->get_read_diskio(), "add_audio_source", Qt::QueuedConnection, qobject_cast<AudioSource*>(m_readSource));
        // re-inits the lenght from the audiofile due calling rsm->set_source_for_clip()
        m_length = TTimeRef();
    }

    QMetaObject::invokeMethod(m_sheet->get_write_diskio(), "remove_and_delete_audio_source", Qt::QueuedConnection, qobject_cast<AudioSource*>(m_writer));

    m_writer = nullptr;

    m_recordingStatus = NO_RECORDING;

    resources_manager()->set_source_for_clip(this, m_readSource);

    emit recordingFinished(this);
}

void AudioClip::finish_recording()
{
    PENTER;

    m_recordingStatus = FINISHING_RECORDING;
    m_writer->set_recording(false);

    disconnect(m_sheet, SIGNAL(transportStopped()), this, SLOT(finish_recording()));
}

uint AudioClip::get_channel_count( ) const
{
    if (m_readSource) {
        return m_readSource->get_channel_count();
    }

    if (m_writer) {
        return m_writer->get_channel_count();
    }


    return 0;
}

Sheet* AudioClip::get_sheet( ) const
{
    Q_ASSERT(m_sheet);
    return m_sheet;
}

AudioTrack* AudioClip::get_track( ) const
{
    Q_ASSERT(m_track);
    return m_track;
}

void AudioClip::set_sheet( Sheet * sheet )
{
    m_sheet = sheet;
    if (m_readSource && m_isReadSourceValid) {
        QMetaObject::invokeMethod(m_sheet->get_read_diskio(), "add_audio_source", Qt::QueuedConnection, qobject_cast<AudioSource*>(m_readSource));
    } else {
        PWARN("AudioClip::set_sheet() : Setting Sheet, but no ReadSource available!!");
    }
    
    m_sheetId = sheet->get_id();
    
    set_history_stack(m_sheet->get_history_stack());
    m_pluginChain->set_session(m_sheet);
    m_locationItem->set_snap_list(m_sheet->get_snap_list());
}


// TODO: this function is (also) called from the GUI thread when moving a clip from one track
//      to another. When this clip is being 'played' during the move, m_track is changed in the gui
//      thread, but we use m_track in ::process(). Could this result in an invalid pointer?
void AudioClip::set_track( AudioTrack * track )
{
    if (m_track) {
        disconnect(m_track, SIGNAL(audibleStateChanged()), this, SLOT(track_audible_state_changed()));
    }

    m_track = track;

    connect(m_track, SIGNAL(audibleStateChanged()), this, SLOT(track_audible_state_changed()));
    set_sources_active_state();
}

bool AudioClip::is_selected( )
{
    Q_ASSERT(m_sheet);
    return m_sheet->get_audioclip_manager()->is_clip_in_selection(this);
}

bool AudioClip::is_take( ) const
{
    return m_isTake;
}

uint AudioClip::get_bitdepth( ) const
{
    if (m_readSource) {
        return m_readSource->get_bit_depth();
    }

    return audiodevice().get_bit_depth();

}

uint AudioClip::get_rate( ) const
{
    if (m_readSource) {
        return m_readSource->get_sample_rate();
    }

    return audiodevice().get_sample_rate();
}

TTimeRef AudioClip::get_source_length( ) const
{
    return m_sourceLength;
}

int AudioClip::recording_state( ) const
{
    return m_recordingStatus;
}

TCommand * AudioClip::normalize( )
{
    bool ok = false;
    audio_sample_t normfactor = 0.0;

    double d = QInputDialog::getDouble(nullptr, tr("Normalization"),
                                       tr("Set normalization level (dB):"), 0.0, -120, 0, 1, &ok);
    if (ok) {
        normfactor = calculate_normalization_factor(audio_sample_t(d));
    } else {
        return ied().failure();
    }

    if (qFuzzyCompare(normfactor, get_gain())) {
        info().information(tr("Requested normalization factor equals actual level, nothing to be done"));
        return ied().failure();
    }

    return new PCommand(this, "set_gain", normfactor, get_gain(), tr("AudioClip: Normalized to %1 dB").arg(d));
}


float AudioClip::calculate_normalization_factor(float targetdB)
{
    float target = dB_to_scale_factor (targetdB);

    if (target == 1.0f) {
        /* do not normalize to precisely 1.0 (0 dBFS), to avoid making it appear
           that we may have clipped.
        */
        target -= FLT_EPSILON;
    }

    audio_sample_t maxamp = m_peak->get_max_amplitude(m_sourceStartLocation, m_sourceEndLocation);

    if (qFuzzyCompare(maxamp, 0.0f)) {
        PWARN("AudioClip::normalization: max amplitude == 0");
        /* don't even try */
        return get_gain();
    }

    if (qFuzzyCompare(maxamp, target)) {
        PWARN("AudioClip::normalization: max amplitude == target amplitude");
        /* we can't do anything useful */
        return get_gain();
    }

    /* compute scale factor */
    return float(target/maxamp);
}

FadeCurve * AudioClip::get_fade_in( ) const
{
    return m_fadeIn;
}

FadeCurve * AudioClip::get_fade_out( ) const
{
    return m_fadeOut;
}

void AudioClip::private_add_fade( FadeCurve* fade )
{
    m_fades.append(fade);

    if (fade->get_fade_type() == FadeCurve::FadeIn) {
        m_fadeIn = fade;
    } else if (fade->get_fade_type() == FadeCurve::FadeOut) {
        m_fadeOut = fade;
    }
}

void AudioClip::private_remove_fade( FadeCurve * fade )
{
    if (fade == m_fadeIn) {
        m_fadeIn = nullptr;
    } else if (fade == m_fadeOut) {
        m_fadeOut = nullptr;
    }

    m_fades.remove(fade);
}

void AudioClip::create_fade(FadeCurve::FadeType fadeType)
{
    FadeCurve* fadeCurve = nullptr;
    switch (fadeType) {
    case FadeCurve::FadeIn:
        m_fadeIn = new FadeCurve(this, FadeCurve::FadeIn);
        fadeCurve = m_fadeIn;
        break;
        case FadeCurve::FadeOut:
        m_fadeOut = new FadeCurve(this, FadeCurve::FadeOut);
        fadeCurve = m_fadeOut;
        break;
    default:
        PERROR("FadeType unknown");
        return;
    }

    Q_ASSERT(fadeCurve);

    fadeCurve->set_shape("Fast");
    fadeCurve->set_history_stack(get_history_stack());
    tsar().add_gui_event(this, fadeCurve, "private_add_fade(FadeCurve*)", "fadeAdded(FadeCurve*)");
}

QDomNode AudioClip::get_dom_node() const
{
    return m_domNode;
}

bool AudioClip::has_sheet() const
{
    return m_sheet != nullptr;
}

ReadSource * AudioClip::get_readsource() const
{
    return m_readSource;
}

void AudioClip::set_as_moving(bool moving)
{
    m_isMoving = moving;
    m_locationItem->set_snappable(!m_isMoving);
    set_sources_active_state();

    // if moving is false, then the user stopped moving this audioclip
    // this is the point where we have to emit positionChanged() to notify
    // AudioTrack and GUI of the changed clip position
    if (!moving) {
        emit positionChanged();
        if (m_track) {
            m_track->clip_position_changed(this);
        }
    }
}

