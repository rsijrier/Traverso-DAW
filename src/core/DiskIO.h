/*
Copyright (C) 2006-2007 Remon Sijrier 

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

#ifndef T_DISKIO_H
#define T_DISKIO_H

#include <QList>
#include <QThread>

#include "RingBufferNPT.h"
#include "TTimeRef.h"
#include "defines.h"

class AudioSource;
class DecodeBuffer;

class DiskIO : public QThread
{
	Q_OBJECT

public:
    DiskIO();
	~DiskIO();
	
	static const int writebuffertime = 5;
	static const int bufferdividefactor = 5;

    void set_transport_location(const TTimeRef& transportLocation) {
        m_transportLocation = transportLocation;
    }

    void set_seek_transport_location(const TTimeRef& transportLocation) {
        m_seekTransportLocation = transportLocation;
        m_waitForSeek.store(true);
    }

    bool get_cpu_time(float &time);
    int get_buffers_fill_status();
    uint get_output_rate() {return m_outputSampleRate;}
	int get_resample_quality() {return m_resampleQuality;}

protected:
    void run() override;

private:
    std::atomic<bool>   m_waitForSeek;

    QList<AudioSource*>	m_audioSources;

    std::atomic<int>    m_bufferFillStatus;

    RingBufferNPT<trav_time_t>*	m_cpuTime;
    trav_time_t         m_lastCpuReadTime;

    int                 m_resampleQuality;
    bool                m_resampleQualityChanged;
    bool                m_sampleRateChanged;
    audio_sample_t*		framebuffer;

    DecodeBuffer*		m_fileDecodeBuffer;
    DecodeBuffer*		m_resampleDecodeBuffer;
    uint                m_outputSampleRate{};

    TTimeRef            m_transportLocation;
    TTimeRef            m_seekTransportLocation;
	
    void stop_disk_thread();    

public slots:
    void seek();

    void add_audio_source(AudioSource* source);
    void remove_and_delete_audio_source(AudioSource* source);

    void set_output_sample_rate(uint outputSampleRate);
    void set_resample_quality(int quality);

private slots:
    void do_work();

signals:
	void seekFinished();
};


#endif

//eof
