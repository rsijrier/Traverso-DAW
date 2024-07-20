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


#include "AudioSource.h"

#include <utility>
#include "Sheet.h"
#include "Peak.h"
#include "Utils.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

// This constructor is called at file import or recording
AudioSource::AudioSource(QString  dir, const QString& name)
	: m_dir(std::move(dir))
	, m_name(name)
	, m_shortName(name)
	, m_wasRecording (false)
{
	PENTERCONS;
	m_fileName = m_dir + m_name;
	m_id = create_id();

    m_rtBufferSlotsQueue = nullptr;
    m_freeBufferSlotsQueue = nullptr;
    m_bufferstatus.set_sync_status(BufferStatus::SyncStatus::OUT_OF_SYNC);

}


// This constructor is called for existing (recorded/imported) audio sources
AudioSource::AudioSource()
	: m_dir("")
	, m_name("")
	, m_fileName("")
	, m_wasRecording(false)
{
    m_rtBufferSlotsQueue = nullptr;
    m_freeBufferSlotsQueue = nullptr;
    m_bufferstatus.set_sync_status(BufferStatus::SyncStatus::OUT_OF_SYNC);

}


AudioSource::~AudioSource()
{
	PENTERDES;
}

void AudioSource::prepare_rt_buffers(nframes_t bufferSize)
{
    m_bufferstatus.set_sync_status(BufferStatus::SyncStatus::OUT_OF_SYNC);

    // printf("AudioSource::prepare_rt_buffers: audio device buffer size %d\n", bufferSize);

    delete_rt_buffers();

    QueueBufferSlot* slot = nullptr;

    m_rtBufferSlotsQueue = new moodycamel::BlockingReaderWriterCircularBuffer<QueueBufferSlot*>(slotcount);
    m_freeBufferSlotsQueue = new moodycamel::BlockingReaderWriterCircularBuffer<QueueBufferSlot*>(slotcount);


    m_bufferSlotDuration = TTimeRef(bufferSize, m_outputRate);

    for (size_t i=0; i<slotcount;++i) {
        slot = new QueueBufferSlot(i, m_channelCount, bufferSize);
        bool queued = m_freeBufferSlotsQueue->try_enqueue(slot);
        Q_ASSERT(queued);
    }

    Q_ASSERT(slot);
    // We have to assign m_lastQueuedRTBufferSlot to an existing slot
    m_lastQueuedRTBufferSlot = slot;

    // printf("AudioSource::::prepare_rt_buffers: freeBufferSlotsQueue slot count %zu\n", m_freeBufferSlotsQueue->size_approx());
    // printf("AudioSource::::prepare_rt_buffers: rtBufferSlotsQueue slot count %zu\n", m_rtBufferSlotsQueue->size_approx());
}

void AudioSource::delete_rt_buffers()
{
    QueueBufferSlot* slot;

    if (m_freeBufferSlotsQueue) {
        while(m_freeBufferSlotsQueue->try_dequeue(slot)) {
            delete slot;
            slot = nullptr;
        }
        Q_ASSERT(m_freeBufferSlotsQueue->size_approx() == 0);
        delete m_freeBufferSlotsQueue;
    }

    if (m_rtBufferSlotsQueue) {
        while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
            delete slot;
            slot = nullptr;
        }
        Q_ASSERT(m_rtBufferSlotsQueue->size_approx() == 0);
        delete m_rtBufferSlotsQueue;
    }
}



void AudioSource::set_name(const QString& name)
{
	m_name = name;
	if (m_wasRecording) {
		m_shortName = m_name.left(m_name.length() - 20);
	} else {
		m_shortName = m_name;
	}
	m_fileName = m_dir + m_name;
}


void AudioSource::set_dir(const QString& dir)
{
	m_dir = dir;
	m_fileName = m_dir + m_name;
}


uint AudioSource::get_sample_rate( ) const
{
	return m_rate;
}

void AudioSource::set_original_bit_depth( uint bitDepth )
{
	m_origBitDepth = bitDepth;
}

void AudioSource::set_created_by_sheet(qint64 id)
{
	m_origSheetId = id;
}

QString AudioSource::get_filename( ) const
{
	return m_fileName;
}

QString AudioSource::get_dir( ) const
{
	return m_dir;
}

QString AudioSource::get_name( ) const
{
	return m_name;
}

uint AudioSource::get_bit_depth( ) const
{
	return m_origBitDepth;
}

QString AudioSource::get_short_name() const
{
	return m_shortName;
}

// eof
