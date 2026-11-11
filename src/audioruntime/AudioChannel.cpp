/*
    Copyright (C) 2005-2019 Remon Sijrier

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

#include "AudioChannel.h"
#include "TRtTransaction.h"

#include "TVUMonitor.h"
#include "TAudioThreadMessageQueue.h"
#include "Debugger.h"

#include <QString>


/**
 * \class AudioChannel
 * \brief AudioChannel wraps one 'real' audiochannel into an easy to use class
 *
 * An AudioChannel has a audio_sample_t* buffer, a name and some functions for setting the buffer size, 
 * and monitoring the highest peak value, which is handy to use by for example a VU meter. 
 */


AudioChannel::AudioChannel(const QString& name, uint channelNumber, int type, nframes_t bufferSize)
    : m_audioBuffer(bufferSize)
{
    m_name = name;
    m_number = channelNumber;
    m_type = type;
    m_monitoring = true;
    m_latency = 0;
}

AudioChannel::~ AudioChannel( )
{
    PENTERDES2;
}

void AudioChannel::set_latency( uint latency )
{
    m_latency = latency;
}

void AudioChannel::set_buffer_size( nframes_t size )
{
    m_audioBuffer.resize(size);
}


void AudioChannel::process_monitoring(TVUMonitor* monitor) T_REALTIME_SAFE
{
    float peakValue = m_audioBuffer.compute_peak();

    if (monitor) {
        monitor->process(peakValue);
    }

    for (TVUMonitor* internalMonitor : m_monitors.rt_items()) {
        internalMonitor->process(peakValue);
    }
}

void AudioChannel::set_monitoring( bool monitor )
{
    m_monitoring = monitor;
}


void AudioChannel::add_monitor(TVUMonitor *monitor)
{
    {
        TRtTransaction transaction;
        m_monitors.add(monitor, transaction);
    }

    emit vuMonitorAdded(monitor);
}

void AudioChannel::remove_monitor(TVUMonitor *monitor)
{
    if (!m_monitors.gui_items().contains(monitor)) {
        TLog::write_printf(TLogLevel::Warning, TLOG_FILE_CATEGORY, "AudioChannel:: VUMonitor was not in monitors list, failed to remove it!");
        return;
    }

    {
        TRtTransaction transaction;
        m_monitors.remove(monitor, transaction);
    }

    emit vuMonitorRemoved(monitor);
}

void AudioChannel::read_from_hardware_port(audio_sample_t *buf, nframes_t nframes) T_REALTIME_SAFE
{
    memcpy (m_audioBuffer.get_data(nframes), buf, sizeof(audio_sample_t) * nframes);

    if (m_monitoring) {
        process_monitoring();
    }
}

void AudioChannel::read_from_hardware_port_interleaved(const audio_sample_t *buf, nframes_t nframes, uint channelCount, uint channelNumber)
{
    audio_sample_t *dst = m_audioBuffer.get_data(nframes);
    for (nframes_t frame = 0; frame < nframes; ++frame) {
        dst[frame] = buf[frame * channelCount + channelNumber];
    }

    if (m_monitoring) {
        process_monitoring();
    }
}


//eof
