/*
Copyright (C) 2006-2026 Remon Sijrier

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

#pragma once

#include <memory>

#include "TAudioFileError.h"
#include "TAudioReader.h"
#include "TAudioResampler.h"
#include "TBufferedAudioStream.h"
#include "TProcessContext.h"

#include <QDomDocument>


class TAudioReader;
struct TAudioIOStats;
class AudioBus;
class TLocation;

class TBufferedAudioStreamReader : public TBufferedAudioStream
{
	Q_OBJECT

public :
    TBufferedAudioStreamReader(const QDomNode &node);
    TBufferedAudioStreamReader(const QString& dir, const QString& name);
    TBufferedAudioStreamReader(const QString& dir, const QString& name, uint channelCount);
    TBufferedAudioStreamReader();  // For creating a 0-channel, silent TBufferedAudioStreamReader
    ~TBufferedAudioStreamReader();
	
	enum ReadSourceError {
        COULD_NOT_OPEN_FILE = -1,
        INVALID_CHANNEL_COUNT = -2,
        ZERO_CHANNELS = -3,
        FILE_DOES_NOT_EXIST = -4
    };
	
    TBufferedAudioStreamReader* deep_copy();

	
	int set_state( const QDomNode& node );
	QDomNode get_state(QDomDocument doc);

    nframes_t ringbuffer_read(TProcessContext &context, const TTimeRef &fileLocation) T_REALTIME_SAFE;

    // count frames at the output rate into buffer, from the file's location
    // (a time); and count frames of the file at its own rate from its frame
    // firstFrame (a waveform works on the file's grid). What the file gives
    // and zeros past its end (TAudioReader).
    TAudioFileResult file_read(TAudioBuffer &buffer, const TTimeRef& fileLocation, nframes_t count) const;
    TAudioFileResult file_read_frames(TAudioBuffer &buffer, nframes_t firstFrame, nframes_t count) const;

    // What the file's reader read and decoded: for the thread that steps
    const TAudioIOStats& io_stats() const;

	int init();
	int get_error() const {return m_errorCode;}
	QString get_error_string() const;
	int set_file(const QString& filename);
    void set_active(bool active);
    bool is_active() const {return m_active.load();}
	
	nframes_t get_nframes() const;
    uint get_file_rate() const;
    const TTimeRef& get_length() const {return m_length;}

    TBufferedAudioStreamStatus& get_buffer_status() final;

    void set_stream_location_window(const TTimeRef& start, const TTimeRef& end) {
        m_streamStart = start;
        m_streamEnd = end;
    }

    void set_source_start_location(const TTimeRef &sourceStartLocation);
	
	
private:
    // The file's reader once init() opened it; none: no file read
    std::unique_ptr<TAudioReader> m_audioReader;

    int                 m_refcount;
    int                 m_errorCode;
    bool                m_silent;
    std::atomic<bool>   m_active;

    TTimeRef            m_streamStart;
    TTimeRef            m_streamEnd;
    TTimeRef            m_length;
    TTimeRef            m_sourceStartLocation;
    TTimeRef            m_aboutOneToFourSecondsTime;

    TQueueBufferSlot* dequeue_from_rt_queue(TProcessContext &context) T_REALTIME_SAFE;
	
	int ref() { return m_refcount++;}
	
	void private_init();

    friend class TResourcesManager;
    friend class TProjectConverter;

    // re-implemented only to be called by DiskIO
    void process_realtime_buffers(TAudioBuffer &fileDecodeBuffer) final;
    void rb_seek_to_transport_location(TAudioBuffer& fileDecodeBuffer, const TTimeRef &transportLocation) final;
    const TAudioIOStats* decoder_stats() const final { return m_audioReader ? &m_audioReader->io_stats() : nullptr; }
    void set_output_rate_and_resampler(int outputRate, const Resampler& resampler) final;
    // Out of its window, a reader needs work when the transport came close
    // (it seeks then) and its fill does not count; otherwise it needs work
    // when it is out of sync or getting empty
    TIOStream::State disk_state(const TTimeRef& transportLocation) final;
    // The transport is near enough to fill the buffers: from about one to
    // four seconds before the stream's start (spread per reader) to its end
    bool transport_in_window(const TTimeRef& transportLocation) const;

signals:
	void stateChanged();
};

