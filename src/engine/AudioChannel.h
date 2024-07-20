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

#ifndef AUDIOCHANNEL_H
#define AUDIOCHANNEL_H

#include "TVUMonitor.h"
#include "defines.h"
#include <QString>
#include <QObject>
#include <QVarLengthArray>
#include "TRealTimeLinkedList.h"

class AudioChannel : public QObject
{
    Q_OBJECT

public:
    AudioChannel(const QString& name, uint channelNumber, int type, qint64 id=0);
    ~AudioChannel();

    enum ChannelFlags {
        ChannelIsInput = 1,
        ChannelIsOutput = 2
    };

    inline audio_sample_t* get_buffer(nframes_t nframes) {
        Q_ASSERT(int(nframes) <= m_buffer.size());
        return m_buffer.data();
    }

    void set_latency(unsigned int latency);

    inline void silence_buffer(nframes_t nframes) {
        Q_ASSERT(int(nframes) <= m_buffer.size());
        memset (m_buffer.data(), 0, sizeof (audio_sample_t) * nframes);
    }

    void set_buffer_size(nframes_t size);
    void set_monitoring(bool monitor);
    void process_monitoring(TVUMonitor* monitor=nullptr);

    void add_monitor(TVUMonitor* monitor);
    void remove_monitor(TVUMonitor* monitor);

    QString get_name() const {return m_name;}
    uint get_number() const {return m_number;}
    uint get_buffer_size() const {return m_bufferSize;}
    int get_type() const {return m_type;}
    qint64 get_id() const {return m_id;}

private:
    TRealTimeLinkedList<TVUMonitor*>    m_monitors;
    QVarLengthArray<audio_sample_t>     m_buffer;
    uint 			m_bufferSize;
    uint 			m_latency;
    uint 			m_number;
    qint64                  m_id;
    int                     m_type;
    bool			mlocked;
    bool			m_monitoring;
    QString 		m_name;

    friend class JackDriver;
    friend class AlsaDriver;
    friend class PADriver;
    friend class PulseAudioDriver;
    friend class TAudioDriver;
    friend class CoreAudioDriver;

    void read_from_hardware_port(audio_sample_t* buf, nframes_t nframes);

private slots:
    void private_add_monitor(TVUMonitor* monitor);
    void private_remove_monitor(TVUMonitor* monitor);

signals:
    void vuMonitorAdded(TVUMonitor*);
    void vuMonitorRemoved(TVUMonitor*);
};

#endif

//eof
