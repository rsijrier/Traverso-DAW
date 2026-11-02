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

#include "TGain.h"
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
#include "TRtTransaction.h"
#include "TProjectManager.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"
#include "TWaveform.h"
#include "TProject.h"
#include "Utils.h"
#include "TConfig.h"
#include "TRecordingLatency.h"
#include "TAudioPluginChain.h"
#include "GainEnvelope.h"
#include "TInputEventDispatcher.h"
#include "Debugger.h"
#include "TFloatCompare.h"

#include <algorithm>

/**
 *	\class TAudioClip
    \brief Represents (part of) an audiofile.
 */

TAudioClip::TAudioClip(const QString& name)
    : TTimeLineEntity()
    , TAudioProcessingNode()
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
    m_recordingStatus = NO_RECORDING;
    m_isReadSourceValid = m_isMoving = false;
    m_isLocked = config().get_property("AudioClip", "LockByDefault", false).toBool();
    m_isTake = false;
    m_syncDuringDrag = false;
    m_fadeIn = nullptr;
    m_fadeOut = nullptr;
    m_fader->automate_port(0, true);

    update_global_configuration();
    connect(&config(), &TConfig::configChanged, this, &TAudioClip::update_global_configuration);
}

TAudioClip::TAudioClip(const QDomNode& node)
    : TAudioClip("")
{
    PENTERCONS;

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
    move_to_location(location);
}

TAudioClip::~TAudioClip()
{
    PENTERDES;
    if (m_readSource) {
        Q_ASSERT(m_sheet);
        m_sheet->remove_audio_source_from_diskio(m_readSource);
    }
    // A project closed while a recording still finished its file (between
    // finish_recording() and finish_write_source())
    if (m_writer) {
        Q_ASSERT(m_sheet);
        m_sheet->remove_audio_source_from_diskio(m_writer);
    }

    delete m_waveform;
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

    {
        // A transaction opened while another one is open joins it: it hands its
        // changes to the outermost one and publishes nothing itself. When a
        // project loads, that is the transaction TProjectManager opens around
        // the load (TSheet::set_state() opens one too, which joins it as well),
        // so the fades reach the audio thread with the whole project in one
        // event. When a clip is copied, this one publishes them.
        TRtTransaction transaction;

        QDomElement fadeInNode = node.firstChildElement("FadeIn");
        if (!fadeInNode.isNull()) {
            if (!m_fadeIn) {
                TFadeCurve* fade = new TFadeCurve(this, TFadeCurve::FadeIn, transaction);
                fade->set_parent_timeline_entity(this);
                add_fade(fade, transaction);
            }
            m_fadeIn->set_state( fadeInNode );
        }

        QDomElement fadeOutNode = node.firstChildElement("FadeOut");
        if (!fadeOutNode.isNull()) {
            if (!m_fadeOut) {
                TFadeCurve* fade = new TFadeCurve(this, TFadeCurve::FadeOut, transaction);
                fade->set_parent_timeline_entity(this);
                add_fade(fade, transaction);
            }
            m_fadeOut->set_state( fadeOutNode );
        }
    }

    QDomNode pluginChainNode = node.firstChildElement("PluginChain");
    if (!pluginChainNode.isNull()) {
        m_pluginChain->set_state(pluginChainNode);
    }

    // Curves rely on our start position, so only set the start location
    // after curves (those created in plugins too!) are inited and having
    // their state set.
    TTimeRef location(e.attribute( "trackstart", "" ).toLongLong(&ok));
    move_to_location(location);

    return 1;
}

QDomNode TAudioClip::get_state( QDomDocument doc )
{
    QDomElement node = doc.createElement("Clip");
    node.setAttribute("trackstart", get_location_start().universal_frame());
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

bool TAudioClip::validate_split_boundary(const TTimeRef& splitLocation) const
{
    TTimeRef start = get_location_start();
    TTimeRef end = get_location_end();

    return (splitLocation > (start + TTimeRef(1)) && splitLocation < (end - TTimeRef(1)));
}

TSplitMutationResult TAudioClip::calculate_split_mutation(const TTimeRef& splitLocation)
{
    Q_ASSERT(validate_split_boundary(splitLocation));
    TSplitMutationResult result;

    // get_clip() returns copies of this clip, including gain and lock state, each
    // with its own read source. Clips must not share a read source: it holds the
    // clip's source start and location window.
    TAudioClip* leftClip = resources_manager()->get_clip(get_id());
    TAudioClip* rightClip = resources_manager()->get_clip(get_id());
    Q_ASSERT(leftClip && rightClip);

    for (TAudioClip* c : {leftClip, rightClip}) {
        // The copy got its read source after its sheet was set, set the sheet again
        // so the read source is added to the sheet's disk io.
        c->set_sheet(m_sheet);
        c->set_track(m_track);
    }

    TTimeRef distanceToSplit = splitLocation - get_location_start();

    // Use the setters, they keep the length and the read source in sync
    leftClip->set_source_end_location(m_sourceStartLocation + distanceToSplit);
    leftClip->move_to_location(get_location_start());
    leftClip->reset_fade_out();

    rightClip->set_source_start_location(m_sourceStartLocation + distanceToSplit);
    rightClip->move_to_location(splitLocation);
    rightClip->reset_fade_in();

    result.toRemove.append(this);
    result.toAdd.append(leftClip);
    result.toAdd.append(rightClip);

    return result;
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
        move_to_location(get_location_start() - movingToLeft);
    } else if (newLeftLocation > get_location_start()) {

        TTimeRef availableTimeRight = m_length;

        TTimeRef movingToRight = newLeftLocation - get_location_start();

        if (movingToRight > (availableTimeRight - TTimeRef(nframes_t(4), get_rate())) ) {
            movingToRight = (availableTimeRight - TTimeRef(nframes_t(4), get_rate()));
        }

        set_source_start_location( m_sourceStartLocation + movingToRight );
        move_to_location(get_location_start() + movingToRight);
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
        set_track_end_location( get_location_end() + movingToRight );

    } else if (newRightLocation < m_location->get_end()) {

        TTimeRef availableTimeLeft = m_length;

        TTimeRef movingToLeft = get_location_end() - newRightLocation;

        if (movingToLeft > availableTimeLeft - TTimeRef(nframes_t(4), get_rate())) {
            movingToLeft = availableTimeLeft - TTimeRef(nframes_t(4), get_rate());
        }

        set_source_end_location( m_sourceEndLocation - movingToLeft);
        set_track_end_location( get_location_end() - movingToLeft );
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

void TAudioClip::move_to_location(const TTimeRef& location)
{
    PENTER2;

    m_location->set_start(location);

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
    m_location->set_end(location);

    if ( (!is_moving()) && m_sheet) {
        m_sheet->get_snap_list()->mark_dirty();
        m_track->clip_position_changed(this);
    }
}

void TAudioClip::set_fade_in_length(const TTimeRef& length)
{
    TRtTransaction transaction;
    if (!m_fadeIn) {
        create_fade(TFadeCurve::FadeIn, transaction);
    }
    m_fadeIn->set_length(length, transaction);
}

void TAudioClip::set_fade_out_length(const TTimeRef& length)
{
    TRtTransaction transaction;
    if (!m_fadeOut) {
        create_fade(TFadeCurve::FadeOut, transaction);
    }
    m_fadeOut->set_length(length, transaction);
}

void TAudioClip::set_selected(bool /*selected*/)
{
    emit stateChanged();
}

//
//  Function called in RealTime AudioThread processing path
//
int TAudioClip::process(TProcessContext &context) T_REALTIME_SAFE
{
    // Handle silence clips
    if (get_channel_count() == 0) {
        return 0;
    }

    nframes_t nframes = context.get_nframes_to_process();
    const TTimeRef startLocation = context.get_start_location();
    const TTimeRef endLocation = context.get_end_location();
    AudioBus* bus = context.get_ringbuffer_read_bus();

    if (m_recordingStatus == RECORDING) {
        process_capture(context);
        return 0;
    }

    // A take being finished has no file to read yet, and no length: a seek
    // during the recording rolls the transport on before it is done
    if (m_recordingStatus != NO_RECORDING) {
        return 0;
    }

    if (!m_isReadSourceValid) {
        return -1;
    }

    if (!m_readSource->is_active()) {
        return 0;
    }

    if (m_isMuted || (TFloatCompare::equals_0(get_gain())) ) {
        return 0;
    }

    if ((startLocation >= m_location->get_end()) || (endLocation <= m_location->get_start())) {
        return 0;
    }

    Q_ASSERT(m_sheet);
    Q_ASSERT(m_readSource);

    TTimeRef fileLocation;
    uint outputRate = m_readSource->get_output_rate();
    // At most the channels of the clip render bus: of a source with more,
    // the rest is not played until the routing knows where they go
    const uint channelcount = std::min(get_channel_count(), bus->get_channel_count());

    // The clip's frames in this cycle, from offset to end: its start and its
    // end rounded onto the cycle's frames, with the one rounding the stream
    // reader fills its slots with (TTimeRef::frame_nearest()). A clip whose
    // first frame rounds into the next cycle has none in this one, nor one
    // whose end rounds to the start of this one: the stream reader decides
    // the same of the same times, so offset < nframes and framesToProcess > 0
    // hold by construction.
    const qint64 first = TTimeRef::frame_nearest(m_location->get_start() - startLocation, outputRate);
    const qint64 end = std::min<qint64>(TTimeRef::frame_nearest(m_location->get_end() - startLocation, outputRate), nframes);
    if (first >= qint64(nframes) || end <= std::max<qint64>(first, 0)) {
        return 0;
    }
    const nframes_t offset = TTimeRef::to_nframes(std::max<qint64>(first, 0));
    const nframes_t framesToProcess = TTimeRef::to_nframes(end) - offset;
    Q_ASSERT(offset < nframes && framesToProcess > 0 && offset + framesToProcess <= nframes);

    fileLocation = (startLocation - m_location->get_start() + m_sourceStartLocation);
    if (offset != 0) {
        TLOG_RT_D("clip", "file location with offset {}", offset);
    }

    // Read the frames from the ringbuffers
    nframes_t readFrames = m_readSource->ringbuffer_read(context, fileLocation);


    if (readFrames == 0) {
        bus->silence_buffers();
        return 0;
    }

    // The slot holds the file on past the clip's end: in the clip's last
    // cycle the frames after its end are silence, so they do not sound with
    // what follows (a clip split there plays them itself, from the same
    // rounded position on)
    const nframes_t clipEnd = offset + framesToProcess;
    if (clipEnd < nframes) {
        for (uint chan = 0; chan < channelcount; ++chan) {
            bus->get_buffer(chan).silence(clipEnd, nframes - clipEnd);
        }
    }

    // The slot holds the file from the clip's first frame to the end of the
    // cycle: the stream reader put its offset where the clip starts, with
    // the same rounding as above
    Q_ASSERT(readFrames == nframes - offset);

    for (TFadeCurve* fade : m_fades.rt_items()) {
        fade->process(context, m_sheet->get_curve_buffer(), bus, startLocation, endLocation, nframes);
    }

    TTimeRef faderEndLocation = fileLocation + TTimeRef(readFrames, outputRate);

    // FIXME: offset is no longer given to fader, so how to deal with partial buffer processing?
    m_fader->process_gain(bus, fileLocation, faderEndLocation, readFrames, channelcount);

    AudioBus* processBus = m_track->get_process_bus();

    // Mixing should be done on the WHOLE buffer, not just part of it
    // so use an unmodified nframes variable
    if (channelcount == 1) {
        TChannelBuffer::mix_buffers_no_gain(processBus->get_buffer(0), bus->get_buffer(0), nframes);
        TChannelBuffer::mix_buffers_no_gain(processBus->get_buffer(1), bus->get_buffer(0), nframes);
    } else if (channelcount == 2) {
        TChannelBuffer::mix_buffers_no_gain(processBus->get_buffer(0), bus->get_buffer(0), nframes);
        TChannelBuffer::mix_buffers_no_gain(processBus->get_buffer(1), bus->get_buffer(1), nframes);
    }

    return 1;
}

//
//  Function called in RealTime AudioThread processing path
//
void TAudioClip::process_capture(TProcessContext &context) T_REALTIME_SAFE
{
    AudioBus* bus = m_track->get_input_bus();

    if (!bus) {
        return;
    }

    context.set_ringbuffer_write_bus(bus);
    nframes_t written = m_writer->ringbuffer_write(context);

    // The take grows past the frames the writer drops only
    const qint64 before = TRecordingLatency::visible_frames(m_capturedFrames, m_captureCompensation);
    m_capturedFrames += written;
    const qint64 after = TRecordingLatency::visible_frames(m_capturedFrames, m_captureCompensation);
    m_length.add_frames(nframes_t(after - before), m_captureRate);

    nframes_t toWrite = context.get_nframes_to_process();
    if (written != toWrite) {
        TLOG_RT_W("clip", "recording buffer: could only write {} of {} frames", written, toWrite);
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
        TLOG_W("clip", "the track's input bus has no channels, not recording");
        return -1;
    }

    // What is played comes in the capture plus the playback latency after
    // the moment it belongs to: the writer drops that many frames first, so
    // the take lines up with what was heard (doc/design/recording-latency.md)
    TAudioDevice& device = m_sheet->audio_device();
    const uint rate = device.get_sample_rate();
    m_captureCompensation = TRecordingLatency::compensation_frames(
        device.get_capture_latency_frames(), device.get_playback_latency_frames(),
        config().get_property("Recording", "ExtraLatency", 0.0).toDouble(), rate,
        bus->get_bus_type() == AudioBus::BusIsHardware, config().get_property("Recording", "LatencyCompensation", true).toBool());
    m_capturedFrames = 0;
    m_captureRate = rate;
    m_sourceStartLocation = TTimeRef();
    TLOG_I("recording", "{}: compensating {} frames of latency", m_name.toStdString(), m_captureCompensation);
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
    spec->set_block_size(device.get_buffer_size());
    spec->set_channel_count(channelcount);
    spec->set_sample_rate(m_sheet->get_project()->project_rate());
    spec->set_export_start_location(TTimeRef());
    spec->set_export_end_location(TTimeRef());
    spec->set_export_file_name(m_name + "-" + sourceid);

    m_writer = new TBufferedAudioStreamWriter(spec->get_export_dir(), spec->get_export_file_name());
    if (m_writer->prepare_file(spec, device.get_sample_rate()) == -1) {
        TLOG_E("clip", "preparing the write source for recording failed");
        delete m_writer;
        m_writer = nullptr;
        delete spec;
        spec = nullptr;
        return -1;
    }
    m_writer->set_process_peaks( true );
    m_writer->set_recording( true );
    m_writer->set_frames_to_drop(m_captureCompensation);

    // Painted from level 0 of its peak file until the recording is finished
    delete m_waveform;
    m_waveform = new TWaveform(m_writer->get_peaks());

    m_sheet->add_audio_source_to_diskio(m_writer);

    // Writers fileFinished() signal comes from DiskIO thread, so we have to connect by Qt::QueuedConnection
    connect(m_writer, &TBufferedAudioStreamWriter::fileFinished, this, &TAudioClip::finish_write_source, Qt::QueuedConnection);
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
        TRtTransaction transaction;
        m_fadeIn->set_length(TTimeRef(), transaction);
    }
}

void TAudioClip::reset_fade_out()
{
    if (m_fadeOut) {
        TRtTransaction transaction;
        m_fadeOut->set_length(TTimeRef(), transaction);
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
    m_readSource->set_source_start_location(get_source_start_location());
    m_readSourceId = rs->get_id();
    m_sourceLength = rs->get_length();
    sync_location_to_stream();
    connect(m_location.get(), &TLocation::locationChanged, this, &TAudioClip::sync_location_to_stream);

    // If m_length isn't set yet, it means we are importing stuff instead of reloading from project file.
    // it's a bit weak this way, hopefull I'll get up something better in the future.
    // The positioning-length-offset and such stuff is still a bit weak :(
    // NOTE: don't change, audio recording (finish_writesource()) assumes there is checked for length == 0 !!!
    if (m_length == TTimeRef()) {
        m_sourceEndLocation = rs->get_length();
        m_length = m_sourceEndLocation;
    }

    set_sources_active_state();

    // A source that is being recorded gets no file waveform: its peak file is
    // the recording's part file until post-processing renames it, and a
    // build of it would be a second writer of the same peak file. The
    // recording's own waveform paints meanwhile.
    if (m_recordingStatus == NO_RECORDING) {
        delete m_waveform;
        m_waveform = nullptr;
        if (m_isReadSourceValid) {
            // Built in the idle time of this clip's sheet's DiskIO
            m_waveform = new TWaveform(rs, [this](const QString& audioPath, const QString& peakPath, uint rate, int channelCount,
                                                  qint64 frames) {
                return m_sheet ? m_sheet->request_peak_build(audioPath, peakPath, rate, channelCount, frames) : TPeakBuild{};
            });
            if (m_sheet) {
                m_waveform->prepare();
            }
        }
    }

    // This will also emit locationChanged() which is more or less what we want.
    set_track_end_location(m_location->get_start() + m_sourceLength - m_sourceStartLocation);
}

void TAudioClip::sync_location_to_stream()
{
    Q_ASSERT(m_readSource);

    m_readSource->set_stream_location_window(get_location_start(), get_location_end());
}

void TAudioClip::finish_write_source()
{
    PENTER;

    Q_ASSERT(m_readSource);
    Q_ASSERT(this->thread() == QThread::currentThread());

    // The writer completed level 0 of the recording's peak file on DiskIO;
    // the levels above it follow in the read DiskIO's idle time. Asked for
    // before the file waveform below asks for its peak file: that one joins
    // the job and shows level 0 meanwhile.
    if (const std::shared_ptr<TPeakStream> peaks = m_writer->get_peaks(); peaks && !peaks->write_failed()) {
        m_sheet->post_process_peaks(peaks);
    }

    // No frames in the file: stopped at once, or within the latency
    // compensation (the writer dropped everything)
    m_emptyTake = m_writer->get_written_frames() == 0;
    if (m_readSource->set_file(m_writer->get_filename()) < 0) {
        if (m_emptyTake) {
            TLOG_W("recording", "{}: no frames recorded", m_name.toStdString());
        } else {
            PERROR("Setting file for ReadSource failed after finishing recording");
        }
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

uint TAudioClip::get_channel_count( ) const T_REALTIME_SAFE
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

    // The peak file is needed before the clip is ever seen
    if (m_waveform) {
        m_waveform->prepare();
    }

    set_history_stack(m_sheet->get_history_stack());
    m_pluginChain->set_session(m_sheet);
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

    Q_ASSERT_X(m_sheet, "TAudioClip::get_bitdepth", "a clip without a source answers its sheet's device");
    return m_sheet->audio_device().get_bit_depth();

}

uint TAudioClip::get_rate( ) const
{
    // The source's rate: the file's
    if (m_readSource) {
        return m_readSource->get_sample_rate();
    }

    // No source yet (a take being recorded): the rate it is written at,
    // its sheet's project's
    Q_ASSERT_X(m_sheet, "TAudioClip::get_rate", "a clip without a source answers its sheet's project rate");
    return m_sheet->get_project()->project_rate();
}

TTimeRef TAudioClip::get_source_length( ) const
{
    return m_sourceLength;
}

int TAudioClip::recording_state( ) const T_REALTIME_SAFE
{
    return m_recordingStatus;
}

float TAudioClip::calculate_normalization_factor(float targetdB)
{
    float target = TGain::from_dB(targetdB);

    if (TFloatCompare::equals_1(target)) {
        /* do not normalize to precisely 1.0 (0 dBFS), to avoid making it appear
           that we may have clipped.
        */
        target -= FLT_EPSILON;
    }

    // From the peak pyramid plus a few samples, at most 0.006 dB above the
    // real peak (the gain is never too high); -1 while the peak
    // file is still being built
    audio_sample_t maxamp = m_waveform ? m_waveform->max_amplitude(m_sourceStartLocation, m_sourceEndLocation) : -1.0f;
    if (maxamp < 0.0f) {
        PWARN("AudioClip::normalization: the waveform's peak data is not ready yet");
        return get_gain();
    }

    if (TFloatCompare::equals_0(maxamp)) {
        PWARN("AudioClip::normalization: max amplitude == 0");
        /* don't even try */
        return get_gain();
    }

    if (TFloatCompare::compare(maxamp, target)) {
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
        set_fade_in_length(TTimeRef());
        Q_ASSERT(m_fadeIn);
    }
    return m_fadeIn;
}

// Returns fade out curve, creates one if it does not exist
TFadeCurve * TAudioClip::get_fade_out()
{
    if (!m_fadeOut) {
        set_fade_out_length(TTimeRef());
        Q_ASSERT(m_fadeOut);
    }
    return m_fadeOut;
}

void TAudioClip::add_fade(TFadeCurve* fade, TRtTransaction& transaction)
{
    m_fades.add(fade, transaction);

    if (fade->get_fade_type() == TFadeCurve::FadeIn) {
        m_fadeIn = fade;
    } else if (fade->get_fade_type() == TFadeCurve::FadeOut) {
        m_fadeOut = fade;
    }
}

// The fade, its first state and its place in the clip reach the audio thread
// together, as part of transaction
TFadeCurve* TAudioClip::create_fade(int fadeType, TRtTransaction& transaction)
{
    if (fadeType != TFadeCurve::FadeIn && fadeType != TFadeCurve::FadeOut) {
        PERROR("FadeType unknown");
        return nullptr;
    }

    // A new fade has the default shape, the "Fast" preset
    TFadeCurve* fadeCurve = new TFadeCurve(this, TFadeCurve::FadeType(fadeType), transaction);
    fadeCurve->set_history_stack(get_history_stack());
    fadeCurve->set_parent_timeline_entity(this);

    add_fade(fadeCurve, transaction);

    emit fadeAdded(fadeCurve);

    return fadeCurve;
}

QDomNode TAudioClip::get_dom_node() const
{
    return m_domNode;
}

bool TAudioClip::has_sheet() const
{
    return m_sheet != nullptr;
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
        if (m_track) {
            m_location->set_moving(false);
            m_track->clip_position_changed(this);
        }
    }
}

