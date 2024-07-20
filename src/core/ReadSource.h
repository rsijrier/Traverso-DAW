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

#ifndef READSOURCE_H
#define READSOURCE_H

#include "AudioSource.h"

#include <QDomDocument>


class ResampleAudioReader;
class AudioBus;
class DecodeBuffer;
class TLocation;

class ReadSource : public AudioSource
{
	Q_OBJECT

public :
	ReadSource(const QDomNode &node);
	ReadSource(const QString& dir, const QString& name);
    ReadSource(const QString& dir, const QString& name, uint channelCount);
	ReadSource();  // For creating a 0-channel, silent ReadSource
	~ReadSource();
	
	enum ReadSourceError {
        COULD_NOT_OPEN_FILE = -1,
        INVALID_CHANNEL_COUNT = -2,
        ZERO_CHANNELS = -3,
        FILE_DOES_NOT_EXIST = -4
    };
	
	ReadSource* deep_copy();
	
	int set_state( const QDomNode& node );
	QDomNode get_state(QDomDocument doc);

    nframes_t ringbuffer_read(AudioBus *audioBus, const TTimeRef &fileLocation, nframes_t frames, bool realTime);

    int file_read(DecodeBuffer* buffer, const TTimeRef& fileLocation, nframes_t cnt) const;
    int file_read(DecodeBuffer* buffer, nframes_t fileLocation, nframes_t cnt);

	int init();
	int get_error() const {return m_error;}
	QString get_error_string() const;
	int set_file(const QString& filename);
	void set_active(bool active);
	
	nframes_t get_nframes() const;
    uint get_file_rate() const;
    const TTimeRef& get_length() const {return m_length;}

    BufferStatus* get_buffer_status() final;

    void set_location(TLocation* location);

    void set_source_start_location(const TTimeRef &sourceStartLocation);
	
	
private:
    ResampleAudioReader*	m_resampleAudioReader;

    DecodeBuffer*       m_fileDecodeBuffer;
    int                 m_refcount;
    int                 m_error;
    bool                m_silent;
    std::atomic<bool>   m_active;

    TLocation*          m_location;
    TTimeRef            m_length;
    TTimeRef            m_sourceStartLocation;
    TTimeRef            m_aboutOneToFourSecondsTime;

    QueueBufferSlot* dequeue_from_rt_queue(bool realTime);
	
	int ref() { return m_refcount++;}
	
	void private_init();

	friend class ResourcesManager;
	friend class ProjectConverter;

    // re-implemented only to be called by DiskIO
    friend class DiskIO;
    void process_realtime_buffers() final;
    void rb_seek_to_transport_location(const TTimeRef &transportLocation) final;
    void set_output_rate_and_convertor_type(int outputRate, int converterType) final;
    void set_decode_buffers(DecodeBuffer * fileReadBuffer, DecodeBuffer *resampleDecodeBuffer);

signals:
	void stateChanged();
};

#endif
