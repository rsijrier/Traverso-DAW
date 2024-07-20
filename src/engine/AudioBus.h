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

$Id: AudioBus.h,v 1.7 2007/06/04 20:47:16 r_sijrier Exp $
*/


#ifndef AUDIOBUS_H
#define AUDIOBUS_H

#include <QObject>
#include <QList>
#include <QString>
#include "TAudioBusConfiguration.h"
#include "defines.h"
#include "AudioChannel.h"

class AudioBus : public QObject
{
    Q_OBJECT

public:
    AudioBus(const TAudioBusConfiguration& config);
    ~AudioBus();

    enum AudioBusFlags {
        BusIsHardware = 1,
        BusIsSoftware = 2
    };


    void add_channel(AudioChannel* chan);
    void add_channel(const QString& channel);
    void audiodevice_params_changed();
    uint get_channel_count() const;
    QStringList get_channel_names() const;
    QString get_name() {return m_name;}

    AudioChannel* get_channel(uint channelNumber);
    QList<AudioChannel* > get_channels() const{return m_channels;}
    QList<qint64> get_channel_ids() const;

    /**
	 *        Get a pointer to the buffer associated with AudioChannel \a channel 
	 * @param channel The channel number to get the buffer from
	 * @param nframes The buffer size to get
	 * @return 
	 */
    audio_sample_t* get_buffer(uint channel, nframes_t nframes) {
        return m_channels.at(channel)->get_buffer(nframes);
    }

    void set_monitoring(bool monitor);
    bool is_input() {return m_type == AudioChannel::ChannelIsInput;}
    bool is_output() {return m_type == AudioChannel::ChannelIsOutput;}
    bool is_valid() const;
    int get_type() const {return m_type;}
    int get_bus_type() const {return m_busType;}
    qint64 get_id() const {return m_id;}
    void set_id(qint64 id) {m_id = id;}
    void set_name(const QString& name) {m_name = name;}

    void process_monitoring() {
        for (int i=0; i<m_channels.size(); ++i) {
            m_channels.at(i)->process_monitoring();
        }
    }

    void process_monitoring(QList<TVUMonitor*> vumonitors) {
        for (int i=0; i<m_channels.size(); ++i) {
            m_channels.at(i)->process_monitoring(vumonitors.at(i));
        }
    }

    /**
	 *        Zero all AudioChannels buffers for
	 * @param nframes size of the buffer
	 */
    void silence_buffers(nframes_t nframes)
    {
        for (int i=0; i<m_channels.size(); ++i) {
            m_channels.at(i)->silence_buffer(nframes);
        }
    }


    AudioBus*               next;

private:
    QList<AudioChannel* >	m_channels;
    QStringList             m_channelNames;
    QString			m_name;

    bool            		m_isMonitoring;
    bool                    m_isInternalBus;
    int                     m_type;
    int                     m_busType;
    qint64                  m_id;

signals:
    void monitoringPeaksStarted();
    void monitoringPeaksStopped();

};


/**
 * Get the AudioChannel associated with \a channelNumber 
 * @param channelNumber The channelNumber associated with this AudioBus's AudioChannel 
 * @return The AudioChannel on succes, 0 on failure
 */
inline AudioChannel * AudioBus::get_channel( uint channelNumber )
{
    if (channelNumber < get_channel_count()) {
        return m_channels.at(channelNumber);
    }
    return nullptr;
}


#endif

//eof
