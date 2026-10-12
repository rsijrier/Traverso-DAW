/*
    Copyright (C) 2026 Remon Sijrier, Ben Levitt

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


#include "TPipeWireDriver.h"
#include "TAudioThreadMessageQueue.h"

#if defined (PIPEWIRE_SUPPORT)

#include "TAudioDevice.h"
#include "AudioChannel.h"
#include "TTimeRef.h"
#include "Debugger.h"

#include <spa/param/audio/format-utils.h>

TPipeWireDriver::TPipeWireDriver(TAudioDevice* device)
    : TAudioDriver(device)
{
    PENTERCONS;

    read = TAudioDriverReadWriteCallBack::from_method<TPipeWireDriver, &TPipeWireDriver::_read>(this);
    write = TAudioDriverReadWriteCallBack::from_method<TPipeWireDriver, &TPipeWireDriver::_write>(this);
    run_cycle = TRunCycleCallBack::from_method<TPipeWireDriver, &TPipeWireDriver::_run_cycle>(this);
}

TPipeWireDriver::~TPipeWireDriver()
{
    PENTERDES;

    if (!m_pwLoop) {
        return ;
    }

    if (m_notifier) {
        m_notifier->setEnabled(false);
        disconnect(m_notifier, &QSocketNotifier::activated, this, &TPipeWireDriver::handle_pipewire_events);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_playbackStream) {
        pw_stream_destroy(m_playbackStream);
        m_playbackStream = nullptr;
    }
    if (m_captureStream) {
        pw_stream_destroy(m_captureStream);
        m_captureStream = nullptr;
    }
    if (m_pwLoop) {
        pw_loop_destroy(m_pwLoop);
        m_pwLoop = nullptr;
    }
    pw_deinit();

}

int TPipeWireDriver::setup_failed(const QString& message)
{
    PENTER;
    emit driverSetupMessage("PipeWire", message, TAudioDevice::DRIVER_SETUP_FAILURE);
    return -1;
}


int TPipeWireDriver::setup(bool capture, bool playback, const QString& /*cardDevice*/)
{
    PENTER;

    m_frameRate = m_device->get_sample_rate();
    m_framesPerCycle = m_device->get_buffer_size();
    m_captureFrameLatency = m_playbackFrameLatency = 0;

    // FIXME:
    uint32_t channels = 2;

    pw_init(nullptr, nullptr);
    m_pwLoop = pw_loop_new(nullptr);

    if (!m_pwLoop) {
        return setup_failed(tr("Could not create PipeWire Main Loop"));
    }

    uint8_t paramBuffer[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(paramBuffer, sizeof(paramBuffer));
    struct spa_audio_info_raw info = {};
    info.format   = SPA_AUDIO_FORMAT_F32P; // 32 bit float non-interleaved buffers
    info.rate     = m_frameRate;
    info.channels = channels;
    info.flags    = 0;

    for (uint32_t i = 0; i < channels; ++i) {
        info.position[i] = (i == 0) ? SPA_AUDIO_CHANNEL_FL : ((i == 1) ? SPA_AUDIO_CHANNEL_FR : SPA_AUDIO_CHANNEL_UNKNOWN);
    }

    const struct spa_pod *duplexParameter = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);
    const struct spa_pod *streamParameters[] = { duplexParameter };
    const enum pw_stream_flags streamFlags = static_cast<enum pw_stream_flags>(
        PW_STREAM_FLAG_MAP_BUFFERS |
        PW_STREAM_FLAG_AUTOCONNECT |
        PW_STREAM_FLAG_RT_PROCESS |
        PW_STREAM_FLAG_INACTIVE
        );

    struct pw_properties *baseProps = pw_properties_new(
        "application.name", "Traverso DAW",
        "application.icon-name", "Traverso",
        "media.type", "Audio",
        "node.link-group", "Traverso_DSP_Group",
        "node.force-quantum", std::to_string(m_framesPerCycle).c_str(),
        "node.force-rate", std::to_string(m_frameRate).c_str(),
        "node.lock-quantum", "true",
        "node.lock-rate", "true",
        "node.latency", std::string(std::to_string(m_framesPerCycle) + "/" + std::to_string(m_frameRate)).c_str(),
        "node.rate", std::string("1/" + std::to_string(m_frameRate)).c_str(),
        nullptr
        );

    if (playback) {
        struct pw_properties *playbackProperties = pw_properties_copy(baseProps);
        pw_properties_set(playbackProperties, "media.name", "Traverso Audio Output");
        pw_properties_set(playbackProperties, "media.category", "Playback");
        pw_properties_set(playbackProperties, "media.class", "Stream/Output/Audio");
        pw_properties_set(playbackProperties, "node.name", "TraversoDAW Playback");
        pw_properties_set(playbackProperties, "node.description", "Traverso DAW Playback");


        std::memset(&m_playbackStreamEvents, 0, sizeof(m_playbackStreamEvents));
        m_playbackStreamEvents.version = PW_VERSION_STREAM_EVENTS;
        m_playbackStreamEvents.process = &TPipeWireDriver::_on_process_playback;
        m_playbackStreamEvents.state_changed = &TPipeWireDriver::_on_state_changed;

        m_playbackStream = pw_stream_new_simple(
            m_pwLoop,
            "TraversoPlaybackStream",
            playbackProperties,
            &m_playbackStreamEvents,
            this
            );

        if (!m_playbackStream) {
            return setup_failed(tr("Could not create PipeWire Playback Stream"));
        }

        int res = pw_stream_connect(
            m_playbackStream,
            PW_DIRECTION_OUTPUT,
            PW_ID_ANY,
            streamFlags,
            streamParameters,
            1
            );

        if (res < 0) {
            return setup_failed(tr("Could not connect Capture stream to server"));
        } else {
            emit driverSetupMessage("PipeWire", tr("Playback Stream connected to server"), TAudioDevice::DRIVER_SETUP_SUCCESS);
        }
    }

    if (capture) {
        struct pw_properties *captureProps = pw_properties_copy(baseProps);
        pw_properties_set(captureProps, "media.name", "Traverso Audio Input");
        pw_properties_set(captureProps, "media.category", "Capture");
        pw_properties_set(captureProps, "media.class", "Stream/Input/Audio");
        pw_properties_set(captureProps, "node.name", "TraversoDAW Capture");
        pw_properties_set(captureProps, "node.description", "Traverso DAW Input");


        std::memset(&m_captureStreamEvents, 0, sizeof(m_captureStreamEvents));
        m_captureStreamEvents.version = PW_VERSION_STREAM_EVENTS;

        m_captureStreamEvents.process = &TPipeWireDriver::_on_process_capture;
        m_captureStreamEvents.state_changed = &TPipeWireDriver::_on_state_changed;

        m_captureStream = pw_stream_new_simple(
            m_pwLoop,
            "TraversoCaptureStream",
            captureProps,
            &m_captureStreamEvents,
            this
            );

        if (!m_captureStream) {
            return setup_failed(tr("Could not create PipeWire Capture Stream"));
        }

        int res = pw_stream_connect(
            m_captureStream,
            PW_DIRECTION_INPUT,
            PW_ID_ANY,
            streamFlags,
            streamParameters,
            1
            );

        if (res < 0) {
            return setup_failed(tr("Could not connect capture stream to server"));
        } else {
            emit driverSetupMessage("PipeWire", tr("Capture Stream connected to server"), TAudioDevice::DRIVER_SETUP_SUCCESS);
        }
    }

    pw_properties_free(baseProps);

    int pipewire_fd = pw_loop_get_fd(m_pwLoop);
    m_notifier = new QSocketNotifier(pipewire_fd, QSocketNotifier::Read, this);
    m_notifier->setEnabled(false);

    connect(m_notifier, &QSocketNotifier::activated, this, &TPipeWireDriver::handle_pipewire_events);

    return 1;
}

int TPipeWireDriver::attach()
{
    PENTER;

    m_device->set_buffer_size (m_framesPerCycle);
    m_device->set_sample_rate (m_frameRate);


    AudioChannel* chan;

    // Create 2 capture channels
    for (uint chn=0; chn<2; chn++) {
        chan = add_capture_channel(QString("capture_%1").arg(chn+1));
        chan->set_latency( m_framesPerCycle + m_captureFrameLatency );
    }

    // Create 2 playback channels
    for (uint chn=0; chn<2; chn++) {
        chan = add_playback_channel(QString("playback_%1").arg(chn+1));
        chan->set_latency( m_framesPerCycle + m_captureFrameLatency );
    }

    return 1;
}

int TPipeWireDriver::start()
{
    PENTER;

    if (m_notifier) {
        m_notifier->setEnabled(true);
    }

    if (m_playbackStream) {
        pw_stream_set_active(m_playbackStream, true);
    }
    if (m_captureStream) {
        pw_stream_set_active(m_captureStream, true);
    }

    TAudioDriver::start();

    return 1;
}


int TPipeWireDriver::stop()
{
    PENTER;
    if (!m_pwLoop) return 1;

    if (m_playbackStream) {
        pw_stream_set_active(m_playbackStream, false);
    }
    if (m_captureStream) {
        pw_stream_set_active(m_captureStream, false);
    }

    if (m_notifier) {
        m_notifier->setEnabled(false);
    }

    TAudioDriver::stop();

    return 1;
}

// Called in RT thread from PipeWire server
int TPipeWireDriver::process_callback()
{
    m_runCycleStartTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_start_time(m_runCycleStartTime);

    int64_t newPlaybackLatency = m_playbackFrameLatency;
    int64_t newCaptureLatency = m_captureFrameLatency;

    if (m_playbackStream) {
        struct pw_time playback_time;
        if (pw_stream_get_time_n(m_playbackStream, &playback_time, sizeof(playback_time)) == 0) {
            int64_t out_delay = playback_time.delay + playback_time.queued + playback_time.buffered;
            if (out_delay < 0) {
                out_delay = playback_time.size;
            }
            newPlaybackLatency = out_delay;
        }
    }

    if (m_captureStream) {
        struct pw_time capture_time;
        if (pw_stream_get_time_n(m_captureStream, &capture_time, sizeof(capture_time)) == 0) {
            int64_t in_delay = capture_time.delay + capture_time.queued + capture_time.buffered;
            if (in_delay < 0) {
                in_delay = capture_time.size;
            }
            newCaptureLatency = in_delay;
        }
    }

    if (newPlaybackLatency != m_playbackFrameLatency || newCaptureLatency != m_captureFrameLatency) {
        m_playbackFrameLatency = newPlaybackLatency;
        m_captureFrameLatency = newCaptureLatency;

        tsmp().post_to_gui_thread(TAudioThreadMessageQueueEvent{
            .rtTask = nullptr,
            .guiCallback = [this]() { emit latencyChanged(); }
        });
    }

    m_device->run_cycle(m_framesPerCycle, 0.0);

    m_runCycleEndTime = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_end_time(m_runCycleEndTime);

    return 1;
}

// Called in RT thread from TAudioDevice
int TPipeWireDriver::_read( nframes_t /*nframes*/ )
{
    // allready got data in _on_process_capture() callback
    return 1;
}

// Called in RT thread from TAudioDevice
int TPipeWireDriver::_write( nframes_t nframes )
{
    struct pw_buffer* b = pw_stream_dequeue_buffer(m_playbackStream);
    if (!b) return -1;

    struct spa_buffer* buf = b->buffer;
    uint channelCount = m_playbackChannels.size();

    if (buf->n_datas < channelCount) {
        channelCount = buf->n_datas;
    }

    for (uint chan = 0; chan < channelCount; ++chan) {
        float* dst = static_cast<float*>(buf->datas[chan].data);

        if (dst) {
            std::memcpy(dst, m_playbackChannels.at(chan)->get_buffer().get_data(0), nframes * sizeof(float));
        }

        if (buf->datas[chan].chunk) {
            buf->datas[chan].chunk->offset = 0;
            buf->datas[chan].chunk->stride = sizeof(float);
            buf->datas[chan].chunk->size = nframes * sizeof(float);
        }
    }

    for (auto channel : std::as_const(m_playbackChannels)) {
        channel->silence_buffer();
    }

    pw_stream_queue_buffer(m_playbackStream, b);
    return 1;
}


// Called in RT thread
int TPipeWireDriver::_run_cycle()
{
    return m_device->run_cycle(m_framesPerCycle, 0);
}

// Called in RT thread from PipeWire server
void TPipeWireDriver::_on_process_playback(void *userdata)
{
    TPipeWireDriver* driver  = static_cast<TPipeWireDriver *> (userdata);

    driver->process_callback();
}

// Called in RT thread from PipeWire server
void TPipeWireDriver::_on_process_capture(void *userdata)
{
    TPipeWireDriver* driver = static_cast<TPipeWireDriver*>(userdata);
    driver->process_capture_callback();
}

// Called in RT thread from PipeWire server
int TPipeWireDriver::process_capture_callback()
{
    struct pw_buffer* b = pw_stream_dequeue_buffer(m_captureStream);
    if (!b) return -1;

    struct spa_buffer* buf = b->buffer;
    uint channelCount = m_captureChannels.size();

    if (buf->n_datas < channelCount) {
        channelCount = buf->n_datas;
    }

    for (uint chan = 0; chan < channelCount; ++chan) {
        if (chan < buf->n_datas && buf->datas[chan].data) {
            float* src = static_cast<float*>(buf->datas[chan].data);
            m_captureChannels.at(chan)->read_from_hardware_port(src, m_framesPerCycle);
        }
    }

    pw_stream_queue_buffer(m_captureStream, b);
    return 1;
}

void TPipeWireDriver::_on_state_changed(void *userdata, enum pw_stream_state old_state, enum pw_stream_state state, const char *error) {
    static_cast<TPipeWireDriver*>(userdata)->handle_state_changed(old_state, state, error);
}


void TPipeWireDriver::handle_state_changed(enum pw_stream_state /*old_state*/, enum pw_stream_state state, const char *error)
{
    PENTER;

    if (state == PW_STREAM_STATE_ERROR && error) {
        emit driverSetupMessage("PipeWire", tr("Stream Error: %1").arg(error), TAudioDevice::DRIVER_SETUP_INFO);
    }
    if (state == PW_STREAM_STATE_UNCONNECTED) {
        emit driverSetupMessage("PipeWire", tr("Stream Unconnected"), TAudioDevice::DRIVER_SETUP_INFO);
    }
    if (state == PW_STREAM_STATE_CONNECTING) {
        emit driverSetupMessage("PipeWire", tr("Stream Connecting"), TAudioDevice::DRIVER_SETUP_INFO);
    }
    if (state == PW_STREAM_STATE_PAUSED) {
        emit driverSetupMessage("PipeWire", tr("Stream Paused"), TAudioDevice::DRIVER_SETUP_INFO);
    }
    if (state == PW_STREAM_STATE_STREAMING) {
        emit driverSetupMessage("PipeWire", tr("Stream Running"), TAudioDevice::DRIVER_SETUP_INFO);
    }
}


void TPipeWireDriver::handle_pipewire_events()
{
    PENTER;
    pw_loop_iterate(m_pwLoop, 0);
}

QString TPipeWireDriver::get_device_name()
{
    return "PipeWire";
}

QString TPipeWireDriver::get_device_longname()
{
    return "PipeWire Audio Server";
}

#endif // PIPEWIRE_SUPPORT
