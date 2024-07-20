/*
Copyright (C) 2005-2007 Remon Sijrier 

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

#ifndef AUDIOSOURCE_H
#define AUDIOSOURCE_H

#include "TTimeRef.h"
#include "Utils.h"
#include "defines.h"

#include "cameron/readerwritercircularbuffer.h"

#include <QObject>

class DecodeBuffer;

struct BufferStatus {

    enum SyncStatus {
        UNKNOWN,
        OUT_OF_SYNC,
        IN_SYNC,
        QUEUE_SEEKING_TO_NEW_LOCATION,
        QUEUE_SEEKED_TO_NEW_LOCATION,
        FILL_RTBUFFER_DEQUEUE_FAILURE,
        FILL_RTBUFFER_ENQUEUE_FAILURE,
    };

    inline bool out_of_sync() const {return m_syncStatus.load() != IN_SYNC;}

    inline void set_sync_status(int status) {
        m_syncStatus.store(status);
    }
    inline int get_sync_status() {
        return m_syncStatus.load();
    }

    int     fillStatus;
    int     priority;

private:
    std::atomic<int>     m_syncStatus;
};

class QueueBufferSlot {
public:
    QueueBufferSlot(int slotNumber, uint channelCount, nframes_t bufferSize) {
        m_fileLocation = TTimeRef::INVALID;
        m_slotNumber = slotNumber;
        m_bufferSize = bufferSize;
        m_channelCount = channelCount;
        for (uint i=0; i<channelCount; ++i) {
            m_buffers.append(new audio_sample_t[bufferSize]);
        }
    }
    ~QueueBufferSlot() {
        for (uint i=0; i<m_channelCount; ++i) {
            delete [] m_buffers.at(i);
        }
    }

    int get_slot_number() const {return m_slotNumber;}
    inline nframes_t get_buffer_size() const {return m_bufferSize;}
    inline TTimeRef get_file_location() const {return m_fileLocation;}

    audio_sample_t* get_buffer(uint channel) {
        Q_ASSERT(channel < m_channelCount);
        return m_buffers.at(channel);
    }

    void read_buffer(audio_sample_t* dest, uint channel, nframes_t nframes) {
        Q_ASSERT(nframes <= m_bufferSize);
        Q_ASSERT(channel < m_channelCount);
        Q_ASSERT(nframes > 0);
        memcpy(dest, m_buffers.at(channel), nframes * sizeof(audio_sample_t));
    }

    void write_buffer(const TTimeRef &fileLocation, audio_sample_t* source, uint channel, nframes_t nframes) {
        Q_ASSERT(nframes == m_bufferSize);
        Q_ASSERT(nframes > 0);
        memcpy(m_buffers.at(channel), source, nframes * sizeof(audio_sample_t));
        m_fileLocation = fileLocation;
    }

    void set_file_location(const TTimeRef& fileLocation) {
        m_fileLocation = fileLocation;
    }

    void print_state() const {
        printf("TransportLocation %s, slotnumber %d\n", QS_C(TTimeRef::timeref_to_ms_3(m_fileLocation)), m_slotNumber);
    }

private:
    TTimeRef            m_fileLocation;
    int                 m_slotNumber;
    nframes_t           m_bufferSize;
    uint                m_channelCount;
    QList<audio_sample_t*>   m_buffers;
};


/// The base class for AudioSources like ReadSource and WriteSource
class AudioSource : public QObject
{
    Q_OBJECT

public :
	AudioSource();
	AudioSource(QString  dir, const QString& name);
        virtual ~AudioSource();
	
	void set_name(const QString& name);
	void set_dir(const QString& name);
	void set_original_bit_depth(uint bitDepth);
	void set_created_by_sheet(qint64 id);
	QString get_filename() const;
	QString get_dir() const;
	QString get_name() const;
	QString get_short_name() const;
        qint64 get_id() const {return m_id;}
	qint64 get_orig_sheet_id() const {return m_origSheetId;}
    uint get_sample_rate() const;
        uint get_channel_count() const {return m_channelCount;}
    uint get_bit_depth() const;

    virtual BufferStatus* get_buffer_status() = 0;
    uint get_output_rate() const {return m_outputRate;}

	
protected:
    BufferStatus		m_bufferstatus;

    moodycamel::BlockingReaderWriterCircularBuffer<QueueBufferSlot*> *m_rtBufferSlotsQueue;
    moodycamel::BlockingReaderWriterCircularBuffer<QueueBufferSlot*> *m_freeBufferSlotsQueue;
    QueueBufferSlot*    m_lastQueuedRTBufferSlot;

    TTimeRef            m_bufferSlotDuration;
    uint                m_outputRate;
    size_t slotcount = 50;

    // Used for WriteSource, change to DecodeBuffer
    audio_sample_t* m_diskIOFramebuffer;

    uint		m_channelCount;
    qint64		m_origSheetId{};
	QString 	m_dir;
	qint64		m_id{};
	QString 	m_name;
	QString		m_shortName;
    uint		m_origBitDepth{};
	QString		m_fileName;
    // FIMXE : use output rate instead ?
    uint 		m_rate{};
	int		m_wasRecording;

private:
    // Creation and deletion of the RT buffers can only happen if we know
    // these buffers are not accessed by the audio thread.
    // DiskIO is used to synchronize communication between audio thread and the
    // other threads so let DiskIO prepare and delete the buffers since it 'knows'
    // when it is save to do so
    friend class DiskIO;
    void prepare_rt_buffers(nframes_t bufferSize);
    void delete_rt_buffers();
    virtual void process_realtime_buffers() = 0;
    virtual void rb_seek_to_transport_location(const TTimeRef &transportLocation) = 0;
    virtual void set_output_rate_and_convertor_type(int outputRate, int converterType) = 0;
    virtual void set_decode_buffers(DecodeBuffer * fileReadBuffer, DecodeBuffer *resampleDecodeBuffer) = 0;
    // Used in WriteSource, change to use DecodeBuffers instead
    void set_diskio_frame_buffer(audio_sample_t* frameBuffer) {
        m_diskIOFramebuffer = frameBuffer;
    }


};


#endif
