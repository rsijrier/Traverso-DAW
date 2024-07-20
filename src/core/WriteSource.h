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

#ifndef WRITESOURCE_H
#define WRITESOURCE_H

#include "AudioSource.h"

#include <samplerate.h>
#include "gdither_types.h"

class TExportSpecification;
class Peak;
class AbstractAudioWriter;
class AudioBus;

/// WriteSource is an AudioSource used for writing (recording, rendering) purposes
class WriteSource : public AudioSource
{
	Q_OBJECT

public :
	WriteSource(TExportSpecification* spec);
	~WriteSource();

    nframes_t ringbuffer_write(AudioBus* bus, nframes_t nframes, bool realTime);
    int rb_file_write(QueueBufferSlot* slot);
	void process_ringbuffer(audio_sample_t* buffer);

    BufferStatus* get_buffer_status() final;

	Peak* get_peak() {return m_peak;}

    nframes_t process(nframes_t nframes);
	
	int prepare_export();
	int finish_export();

	void set_process_peaks(bool process);
    void set_recording(bool rec);

    bool is_recording() const;

private:
	AbstractAudioWriter*	m_writer;
	TExportSpecification*	m_exportSpecification;
    Peak*                   m_peak;
	
    GDither         m_dither;
    bool            m_processPeaks;
    bool            m_isRecording;
    nframes_t       m_sampleRate;
    uint32_t        m_sampleBytes;
	
	// Sample rate conversion variables
    nframes_t       m_outSamplesMax;
    nframes_t       m_leftOverFrames;
    SRC_DATA        m_srcData{};
    SRC_STATE*      m_srcState;
    nframes_t       m_leftOverBufferSize; // in frames to hold interleaved data
    float*          m_leftOverBuffer;
    float*          m_dataBuffer;
    void*           m_outputData;

    QueueBufferSlot* dequeue_from_free_queue(bool realTime);

    friend class DiskIO;
    void process_realtime_buffers() final;
    void rb_seek_to_transport_location(const TTimeRef &/*transportLocation*/) final {
        // WriteSource does not support seeking atm
    }
    void set_output_rate_and_convertor_type(int /*outputRate*/, int /*converterType*/) final {
        // WriteSource does not support rate/convert type change atm
    }
    void set_decode_buffers(DecodeBuffer * /*fileReadBuffer*/, DecodeBuffer */*resampleDecodeBuffer*/) final {
        // Writesource does not support DecodeBuffers yet
    }



signals:
	void exportFinished();
};

inline bool WriteSource::is_recording( ) const
{
	return m_isRecording;
}

#endif




