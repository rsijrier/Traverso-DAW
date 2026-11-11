/*
Copyright (C) 2005-2006 Remon Sijrier 

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

$Id: AudioChannel.h,v 1.8 2008/11/24 21:11:04 r_sijrier Exp $
*/

#pragma once

#include "TChannelBuffer.h"
#include "TTypes.h"
#include "TRealTime.h"
#include <QString>
#include <QObject>
#include "TRtList.h"

class TVUMonitor;

class AudioChannel : public QObject
{
    Q_OBJECT

public:
    explicit AudioChannel(const QString& name, uint channelNumber, int type, nframes_t bufferSize);
    ~AudioChannel();

    enum ChannelFlags {
        ChannelIsInput = 1,
        ChannelIsOutput = 2
    };

    TChannelBuffer& get_buffer() {
        return m_audioBuffer;
    }

    void set_latency(unsigned int latency);

    inline void silence_buffer() {
        m_audioBuffer.silence_data();
    }

    void set_buffer_size(nframes_t size);
    void set_monitoring(bool monitor);
    void process_monitoring(TVUMonitor* monitor=nullptr) T_REALTIME_SAFE;

    void add_monitor(TVUMonitor* monitor);
    void remove_monitor(TVUMonitor* monitor);

    QString get_name() const {return m_name;}
    uint get_number() const {return m_number;}
    int get_type() const {return m_type;}

private:
    TRtList<TVUMonitor>     m_monitors;
    TRealTimeChannelBuffer    m_audioBuffer;
    uint 			m_latency;
    uint 			m_number;
    int                     m_type;
    bool			m_monitoring;
    QString 		m_name;

    friend class TJackDriver;
    friend class TAlsaDriver;
    friend class TPortAudioDriver;
    friend class TAudioDriver;
    friend class TCoreAudioDriver;
    friend class TPipeWireDriver;

    void read_from_hardware_port(audio_sample_t* buf, nframes_t nframes) T_REALTIME_SAFE;
    void read_from_hardware_port_interleaved(const audio_sample_t* buf, nframes_t nframes, uint channelCount, uint channelNumber);

signals:
    void vuMonitorAdded(TVUMonitor*);
    void vuMonitorRemoved(TVUMonitor*);
};


//eof
