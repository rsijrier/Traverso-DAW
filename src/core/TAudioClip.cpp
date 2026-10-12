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

#include <cfloat>

#include "TAudioThreadMessageQueue.h"
#include "TContextItem.h"
#include "TBufferedAudioStreamReader.h"
#include "TAudioClip.h"
#include "TLocation.h"
#include "TBufferedAudioStreamWriter.h"
#include "TSheet.h"
#include "TSnapList.h"
#include "TAudioTrack.h"
#include "AudioBus.h"
#include "TAudioDevice.h"
#include "TExportSpecification.h"
#include "TAudioClipManager.h"
#include "TResourcesManager.h"
#include "TCurve.h"
#include "TFadeCurve.h"
#include "TProjectManager.h"
#include "TPeak.h"
#include "TProject.h"
#include "Utils.h"
#include "TConfig.h"
#include "TAudioPluginChain.h"
#include "GainEnvelope.h"
#include "TInputEventDispatcher.h"
#include "Debugger.h"

/**
 *	\class TAudioClip
    \brief Represents (part of) an audiofile.
 */

TAudioClip::TAudioClip(const QString& name)
{
    PENTERCONS;
    m_name = name;
    m_isMuted=false;
    m_readSourceId = m_sheetId = 0;
    QObject::tr("TAudioClip");

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
    m_location = new TLocation(this);

    // read in the configuration from the global configuration settings.
    update_global_configuration();
    connect(&config(), &TConfig::configChanged, this, &TAudioClip::update_global_configuration);
}

TAudioClip::TAudioClip(const QDomNode& node)
    : TAudioClip("")
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
    set_location_start(location);
}

TAudioClip::~TAudioClip()
{
    PENTERDES;
    if (m_readSource) {
        m_sheet->remove_audio_source_from_diskio(m_readSource);
    }

    if (m_peak) {
        m_peak->close();
    }
}

void TAudioClip::init()
{
}

void TAudioClip::update_global_configuration()
{
    m_syncDuringDrag = config().get_property("AudioClip", "SyncDuringDrag", false).toBool();
}

int TAudioClip::set_state(const QDomNode& node)
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
            m_fadeIn = new TFadeCurve(this, TFadeCurve::FadeIn);
            m_fadeIn->set_parent_location(m_location);

            private_add_fade(m_fadeIn);
        }
        m_fadeIn->set_state( fadeInNode );
    }

    QDomElement fadeOutNode = node.firstChildElement("FadeOut");
    if (!fadeOutNode.isNull()) {
        if (!m_fadeOut) {
            m_fadeOut = new TFadeCurve(this, TFadeCurve::FadeOut);
            m_fadeOut->set_parent_location(m_location);
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
    set_location_start(location);

    return 1;
}

QDomNode TAudioClip::get_state( QDomDocument doc )
{
    QDomElement node = doc.createElement("Clip");
    node.setAttribute("trackstart", m_location->get_start().universal_frame());
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

void TAudioClip::toggle_mute()
{
    PENTER;

    TAudioProcessingNode::set_muted(!m_isMuted);
    set_sources_active_state();
}

void TAudioClip::toggle_lock()
{
    m_isLocked = !m_isLocked;
    emit lockChanged();
}

void TAudioClip::track_audible_state_changed()
{
    set_sources_active_state();
}

void TAudioClip::set_sources_active_state()
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

void TAudioClip::removed_from_track()
{
    m_readSource->set_active(false);
}

void TAudioClip::set_left_edge(const TTimeRef &location)
{
    TTimeRef newLeftLocation = location;

    if (newLeftLocation < TTimeRef()) {
        newLeftLocation = TTimeRef();
    }

    if (newLeftLocation < get_location_start()) {

        TTimeRef availableTimeLeft = extandable_lenght_left();

        TTimeRef movingToLeft = get_location_start() - newLeftLocation;

        if (movingToLeft > availableTimeLeft) {
            movingToLeft = availableTimeLeft;
        }

        set_source_start_location( m_sourceStartLocation - movingToLeft );
        set_location_start(get_location_start() - movingToLeft);
    } else if (newLeftLocation > get_location_start()) {

        TTimeRef availableTimeRight = m_length;

        TTimeRef movingToRight = newLeftLocation - get_location_start();

        if (movingToRight > (availableTimeRight - TTimeRef(nframes_t(4), get_rate())) ) {
            movingToRight = (availableTimeRight - TTimeRef(nframes_t(4), get_rate()));
        }

        set_source_start_location( m_sourceStartLocation + movingToRight );
        set_location_start(get_location_start() + movingToRight);
    }
    emit edgeMoved(true);
}

void TAudioClip::set_right_edge(const TTimeRef &location)
{
    TTimeRef newRightLocation = location;

    if (newRightLocation < TTimeRef()) {
        newRightLocation = TTimeRef();
    }

    if (newRightLocation > get_location_end()) {

        TTimeRef availableTimeRight = extandable_length_right();

        TTimeRef movingToRight = newRightLocation - get_location_end();

        if (movingToRight > availableTimeRight) {
            movingToRight = availableTimeRight;
        }

        set_source_end_location( m_sourceEndLocation + movingToRight );
        set_track_end_location( m_location->get_end() + movingToRight );

    } else if (newRightLocation < m_location->get_end()) {

        TTimeRef availableTimeLeft = m_length;

        TTimeRef movingToLeft = m_location->get_end() - newRightLocation;

        if (movingToLeft > availableTimeLeft - TTimeRef(nframes_t(4), get_rate())) {
            movingToLeft = availableTimeLeft - TTimeRef(nframes_t(4), get_rate());
        }

        set_source_end_location( m_sourceEndLocation - movingToLeft);
        set_track_end_location( m_location->get_end() - movingToLeft );
    }
    emit edgeMoved(false);
}

TTimeRef TAudioClip::extandable_length_right() const
{
    return m_sourceLength - m_sourceEndLocation;
}

TTimeRef TAudioClip::extandable_lenght_left() const
{
    return m_sourceStartLocation;
}

void TAudioClip::set_source_start_location(const TTimeRef& location)
{
    Q_ASSERT(m_readSource);

    m_sourceStartLocation = location;
    m_readSource->set_source_start_location(location);
    m_length = m_sourceEndLocation - m_sourceStartLocation;
}

void TAudioClip::set_source_end_location(const TTimeRef& location)
{
    m_sourceEndLocation = location;
    m_length = m_sourceEndLocation - m_sourceStartLocation;
}

void TAudioClip::set_location_start(const TTimeRef& location)
{
    PENTER2;

    m_location->set_start(this, location);

    m_fader->get_curve()->set_start_offset(m_location->get_start());

    // set_track_end_location will emit locationChanged(), so we
    // don't emit it in this function to avoid emitting it twice
    // (although it seems more logical to emit it here, there are
    // situations where only set_track_end_location() is called, and
    // then we also want to emit locationChanged())
    set_track_end_location(m_location->get_start() + m_length);
}

void TAudioClip::set_track_end_location(const TTimeRef& location)
{
    m_location->set_end(this, location);

    if ( (!is_moving()) && m_sheet) {
        m_sheet->get_snap_list()->mark_dirty();
        m_track->clip_position_changed(this);
    }
}

void TAudioClip::set_fade_in_range(double range)
{
    if (!m_fadeIn) {
        create_fade(TFadeCurve::FadeIn);
    }
    m_fadeIn->set_range(range);
}

void TAudioClip::set_fade_out_range(double range)
{
    if (!m_fadeOut) {
        create_fade(TFadeCurve::FadeOut);
    }
    m_fadeOut->set_range(range);
}

void TAudioClip::set_selected(bool /*selected*/)
{
    emit stateChanged();
}

//
//  Function called in RealTime AudioThread processing path
//
int TAudioClip::process(TProcessCallBackData &processData)
{
    // Handle silence clips
    if (get_channel_count() == 0) {
        return 0;
    }

    nframes_t nframes = processData.get_nframes_to_process();
    const TTimeRef startLocation = processData.get_start_location();
    const TTimeRef endLocation = processData.get_end_location();
    AudioBus* bus = processData.get_ringbuffer_read_bus();

    if (m_recordingStatus == RECORDING) {
        process_capture(processData);
        return 0;
    }

    if (!m_isReadSourceValid) {
        return -1;
    }

    if (!m_readSource->is_active()) {
        return 0;
    }

    if (m_isMuted || (TraversoDAW::Float::equals_0(get_gain())) ) {
        return 0;
    }

    if ((startLocation >= m_location->get_end()) || (endLocation <= m_location->get_start())) {
        return 0;
    }

    Q_ASSERT(m_sheet);
    Q_ASSERT(m_readSource);

    TTimeRef fileLocation;
    nframes_t framesToProcess = nframes;
    nframes_t offset = 0;
    uint outputRate = m_readSource->get_output_rate();
    uint channelcount = get_channel_count();
    Q_ASSERT(bus->get_channel_count() >= channelcount);

    if (startLocation < m_location->get_start()) {
        offset = TTimeRef::to_frame(m_location->get_start() - startLocation, outputRate);
        framesToProcess -= offset;
        printf("framesToProcess %d\n", framesToProcess);
        Q_ASSERT(offset < nframes);
        Q_ASSERT(framesToProcess > 0);
    }

    fileLocation = (startLocation - m_location->get_start() + m_sourceStartLocation);
    if (offset != 0) {
        printf("AudioClip: fileLocation using offset is %s, %d\n", QS_C(TTimeRef::timeref_to_ms_3(fileLocation)), offset);
    }

    if (m_location->get_end() < endLocation) {
        framesToProcess -= TTimeRef::to_frame(endLocation - m_location->get_end(), outputRate);
        Q_ASSERT(framesToProcess > 0);
    }

    // Read the frames from the ringbuffers
    nframes_t readFrames = m_readSource->ringbuffer_read(processData, fileLocation);


    if (readFrames == 0) {
        bus->silence_buffers();
        return 0;
    }

    if (readFrames != framesToProcess) {
        std::cout << QString("AudioClip::process(): readFrames %1, framesToProcess %2").arg(readFrames).arg(framesToProcess).toLatin1().data() << &std::endl;
    }

    for(TFadeCurve* fade = m_fades.first(); fade != nullptr; fade = fade->next) {
        fade->process(m_sheet->get_curve_buffer(), bus, startLocation, endLocation, nframes);
    }

    TTimeRef faderEndLocation = fileLocation + TTimeRef(readFrames, outputRate);

    // FIXME: offset is no longer given to fader, so how to deal with partial buffer processing?
    m_fader->process_gain(bus, fileLocation, faderEndLocation, readFrames, channelcount);

    AudioBus* processBus = m_track->get_process_bus();

    // Mixing should be done on the WHOLE buffer, not just part of it
    // so use an unmodified nframes variable
    if (channelcount == 1) {
        TAudioBuffer::mix_buffers_no_gain(processBus->get_buffer(0), bus->get_buffer(0), nframes);
        TAudioBuffer::mix_buffers_no_gain(processBus->get_buffer(1), bus->get_buffer(0), nframes);
    } else if (channelcount == 2) {
        TAudioBuffer::mix_buffers_no_gain(processBus->get_buffer(0), bus->get_buffer(0), nframes);
        TAudioBuffer::mix_buffers_no_gain(processBus->get_buffer(1), bus->get_buffer(1), nframes);
    }

    return 1;
}

//
//  Function called in RealTime AudioThread processing path
//
void TAudioClip::process_capture(TProcessCallBackData &processData)
{
    AudioBus* bus = m_track->get_input_bus();

    if (!bus) {
        return;
    }

    processData.set_ringbuffer_write_bus(bus);
    nframes_t written = m_writer->ringbuffer_write(processData);

    m_length.add_frames(written, get_rate());

    nframes_t toWrite = processData.get_nframes_to_process();
    if (written != toWrite) {
        printf("AudioClip::process_capture: couldn't write nframes %d to recording buffer for channel 0, only %d\n", toWrite, written);
    }
}

int TAudioClip::init_recording()
{
    Q_ASSERT(m_sheet);
    Q_ASSERT(m_track);

    AudioBus* bus = m_track->get_input_bus();
    uint channelcount = bus->get_channel_count();

    if (channelcount == 0) {
        // Can't record from a Bus with no channels!
        printf("AudioClip::init_recording(): Track input bus has zero channels, not recording\n");
        return -1;
    }

    m_sourceStartLocation = TTimeRef();
    m_isTake = true;
    m_recordingStatus = RECORDING;

    TBufferedAudioStreamReader* rs = resources_manager()->create_recording_source(
                m_sheet->get_audio_sources_dir(),
                m_name, channelcount, m_sheet->get_id());

    resources_manager()->set_source_for_clip(this, rs);

    QString sourceid = QString::number(rs->get_id());

    auto spec = new TExportSpecification;

    spec->set_export_dir(m_sheet->get_audio_sources_dir());

    QString fileFormat = config().get_property("Recording", "FileFormat", "wav").toString();
    if (fileFormat == "wavpack") {
        spec->set_file_format(TraversoDAW::FileFormat::WAVPACK);
        QString compression = config().get_property("Recording", "WavpackCompressionType", "fast").toString();
        QString skipwvx = config().get_property("Recording", "WavpackSkipWVX", "false").toString();
        spec->add_extra_format("quality", compression);
        spec->add_extra_format("skip_wvx", skipwvx);
    }
    else if (fileFormat == "w64") {
        spec->set_file_format(TraversoDAW::FileFormat::W64);
    } else {
        spec->set_file_format(TraversoDAW::FileFormat::WAV);
    }

    spec->set_recording_state(TExportSpecification::RecordingState::RECORDING);
    spec->set_block_size(audiodevice().get_buffer_size());
    spec->set_channel_count(channelcount);
    spec->set_sample_rate(audiodevice().get_sample_rate());
    spec->set_export_start_location(TTimeRef());
    spec->set_export_end_location(TTimeRef());
    spec->set_export_file_name(m_name + "-" + sourceid);

    m_writer = new TBufferedAudioStreamWriter(spec->get_export_dir(), spec->get_export_file_name());
    if (m_writer->prepare_export(spec) == -1) {
        printf("AudioClip::init_recording(): WriteSource prepare_export() failed\n");
        delete m_writer;
        m_writer = nullptr;
        delete spec;
        spec = nullptr;
        return -1;
    }
    m_writer->set_process_peaks( true );
    m_writer->set_recording( true );

    m_sheet->add_audio_source_to_diskio(m_writer);

    // Writers exportFinished() signal comes from DiskIO thread, so we have to connect by Qt::QueuedConnection
    connect(m_writer, &TBufferedAudioStreamWriter::exportFinished, this, &TAudioClip::finish_write_source, Qt::QueuedConnection);
    connect(m_sheet, &TSheet::transportStopped, this, &TAudioClip::finish_recording);

    return 1;
}

TCommand* TAudioClip::lock()
{
    toggle_lock();
    return nullptr;
}

void TAudioClip::reset_fade_in()
{
    if (m_fadeIn) {
        m_fadeIn->set_range(0.99);
    }
}

void TAudioClip::reset_fade_out()
{
    if (m_fadeOut) {
        m_fadeOut->set_range(0.99);
    }
}

TAudioClip* TAudioClip::create_copy( )
{
    PENTER;
    Q_ASSERT(m_sheet);
    Q_ASSERT(m_track);
    QDomDocument doc("AudioClip");
    QDomNode clipState = get_state(doc);
    auto clip = new TAudioClip(m_name);
    clip->set_sheet(m_sheet);
    clip->set_track(m_track);
    clip->set_state(clipState);
    return clip;
}

void TAudioClip::set_audio_source(TBufferedAudioStreamReader* rs)
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
    m_readSource->set_location(m_location);
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
            m_peak = new TPeak(rs);
        }
    }

    // This will also emit locationChanged() which is more or less what we want.
    set_track_end_location(m_location->get_start() + m_sourceLength - m_sourceStartLocation);
}

void TAudioClip::finish_write_source()
{
    PENTER;

    Q_ASSERT(m_readSource);
    Q_ASSERT(this->thread() == QThread::currentThread());

    if (m_readSource->set_file(m_writer->get_filename()) < 0) {
        PERROR("Setting file for ReadSource failed after finishing recording");
    } else {
        m_sheet->add_audio_source_to_diskio(m_readSource);
        // re-inits the lenght from the audiofile due calling rsm->set_source_for_clip()
        m_length = TTimeRef();
    }

    m_sheet->remove_audio_source_from_diskio(m_writer);

    m_writer = nullptr;

    m_recordingStatus = NO_RECORDING;

    resources_manager()->set_source_for_clip(this, m_readSource);

    emit recordingFinished(this);
}

void TAudioClip::finish_recording()
{
    PENTER;

    m_recordingStatus = FINISHING_RECORDING;
    m_writer->set_recording(false);

    disconnect(m_sheet, &TSheet::transportStopped, this, &TAudioClip::finish_recording);
}

uint TAudioClip::get_channel_count( ) const
{
    if (m_readSource) {
        return m_readSource->get_channel_count();
    }

    if (m_writer) {
        return m_writer->get_channel_count();
    }


    return 0;
}

TSheet* TAudioClip::get_sheet( ) const
{
    Q_ASSERT(m_sheet);
    return m_sheet;
}

TAudioTrack* TAudioClip::get_track( ) const
{
    Q_ASSERT(m_track);
    return m_track;
}

void TAudioClip::set_sheet( TSheet * sheet )
{
    m_sheet = sheet;
    if (m_readSource && m_isReadSourceValid) {
        m_sheet->add_audio_source_to_diskio(m_readSource);
    } else {
        PWARN("AudioClip::set_sheet() : Setting Sheet, but no ReadSource available!!");
    }
    
    m_sheetId = sheet->get_id();
    
    set_history_stack(m_sheet->get_history_stack());
    m_pluginChain->set_session(m_sheet);
    m_location->set_snap_list(m_sheet->get_snap_list());
}


// TODO: this function is (also) called from the GUI thread when moving a clip from one track
//      to another. When this clip is being 'played' during the move, m_track is changed in the gui
//      thread, but we use m_track in ::process(). Could this result in an invalid pointer?
void TAudioClip::set_track( TAudioTrack * track )
{
    if (m_track) {
        disconnect(m_track, &TTrack::audibleStateChanged, this, &TAudioClip::track_audible_state_changed);
    }

    m_track = track;

    connect(m_track, &TTrack::audibleStateChanged, this, &TAudioClip::track_audible_state_changed);
    set_sources_active_state();
}

bool TAudioClip::is_selected( )
{
    Q_ASSERT(m_sheet);
    return m_sheet->get_audioclip_manager()->is_clip_in_selection(this);
}

bool TAudioClip::is_take( ) const
{
    return m_isTake;
}

uint TAudioClip::get_bitdepth( ) const
{
    if (m_readSource) {
        return m_readSource->get_bit_depth();
    }

    return audiodevice().get_bit_depth();

}

uint TAudioClip::get_rate( ) const
{
    if (m_readSource) {
        return m_readSource->get_sample_rate();
    }

    return audiodevice().get_sample_rate();
}

TTimeRef TAudioClip::get_source_length( ) const
{
    return m_sourceLength;
}

int TAudioClip::recording_state( ) const
{
    return m_recordingStatus;
}

float TAudioClip::calculate_normalization_factor(float targetdB)
{
    float target = dB_to_scale_factor (targetdB);

    if (TraversoDAW::Float::equals_1(target)) {
        /* do not normalize to precisely 1.0 (0 dBFS), to avoid making it appear
           that we may have clipped.
        */
        target -= FLT_EPSILON;
    }

    audio_sample_t maxamp = m_peak->get_max_amplitude(m_sourceStartLocation, m_sourceEndLocation);

    if (TraversoDAW::Float::equals_0(maxamp)) {
        PWARN("AudioClip::normalization: max amplitude == 0");
        /* don't even try */
        return get_gain();
    }

    if (TraversoDAW::Float::compare(maxamp, target)) {
        PWARN("AudioClip::normalization: max amplitude == target amplitude");
        /* we can't do anything useful */
        return get_gain();
    }

    /* compute scale factor */
    return float(target/maxamp);
}

// Returns fade in curve, creates one if it does not exist
TFadeCurve * TAudioClip::get_fade_in()
{
    if (!m_fadeIn) {
        set_fade_in_range(1.0);
        Q_ASSERT(m_fadeIn);
    }
    return m_fadeIn;
}

// Returns fade out curve, creates one if it does not exist
TFadeCurve * TAudioClip::get_fade_out()
{
    if (!m_fadeOut) {
        set_fade_out_range(1.0);
        Q_ASSERT(m_fadeOut);
    }
    return m_fadeOut;
}

void TAudioClip::private_add_fade( TFadeCurve* fade )
{
    m_fades.append(fade);

    if (fade->get_fade_type() == TFadeCurve::FadeIn) {
        m_fadeIn = fade;
    } else if (fade->get_fade_type() == TFadeCurve::FadeOut) {
        m_fadeOut = fade;
    }
}

void TAudioClip::private_remove_fade( TFadeCurve * fade )
{
    if (fade == m_fadeIn) {
        m_fadeIn = nullptr;
    } else if (fade == m_fadeOut) {
        m_fadeOut = nullptr;
    }

    m_fades.remove(fade);
}

void TAudioClip::create_fade(int fadeType)
{
    TFadeCurve* fadeCurve = nullptr;
    switch (fadeType) {
    case TFadeCurve::FadeIn:
        m_fadeIn = new TFadeCurve(this, TFadeCurve::FadeIn);
        fadeCurve = m_fadeIn;
        break;
        case TFadeCurve::FadeOut:
        m_fadeOut = new TFadeCurve(this, TFadeCurve::FadeOut);
        fadeCurve = m_fadeOut;
        break;
    default:
        PERROR("FadeType unknown");
        return;
    }

    Q_ASSERT(fadeCurve);

    fadeCurve->set_shape("Fast");
    fadeCurve->set_history_stack(get_history_stack());
    fadeCurve->set_parent_location(m_location);

    tsmp().post_rt_task(
        [this, fadeCurve]() { private_add_fade(fadeCurve); }, // 1. Real-time audio loop processing task
        [this, fadeCurve]() { emit fadeAdded(fadeCurve); }    // 2. Main GUI thread return signaling notification
        );

}

QDomNode TAudioClip::get_dom_node() const
{
    return m_domNode;
}

bool TAudioClip::has_sheet() const
{
    return m_sheet != nullptr;
}

TTimeRef TAudioClip::get_location_start() const
{
    return m_location->get_start();
}

TTimeRef TAudioClip::get_location_end() const
{
    return m_location->get_end();
}

void TAudioClip::set_as_moving(bool moving)
{
    m_isMoving = moving;
    m_location->set_snappable(!m_isMoving);
    set_sources_active_state();

    // if moving is false, then the user stopped moving this audioclip
    // this is the point where we have to emit locationChanged() to notify
    // AudioTrack and GUI of the changed clip position
    if (!moving) {
        emit m_location->locationChanged();
        if (m_track) {
            m_track->clip_position_changed(this);
        }
    }
}

