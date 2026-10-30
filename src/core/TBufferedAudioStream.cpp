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


#include "TBufferedAudioStream.h"

#include <utility>
#include "TSheet.h"
#include "Utils.h"
#include "TQueueBufferSlot.h"
#include "TIOPlanner.h"
#include "TLog.h"
#include "TAudioIOStats.h"

#include <chrono>



#include "Debugger.h"

// This constructor is called at file import or recording
TBufferedAudioStream::TBufferedAudioStream(QString  dir, const QString& name)
	: m_dir(std::move(dir))
	, m_name(name)
	, m_shortName(name)
	, m_wasRecording (false)
{
	PENTERCONS;
	m_fileName = m_dir + m_name;
    m_id = TraversoDAW::Utils::create_id();

    m_rtBufferSlotsQueue = nullptr;
    m_freeBufferSlotsQueue = nullptr;
    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::SyncStatus::OUT_OF_SYNC);

}


// This constructor is called for existing (recorded/imported) audio sources
TBufferedAudioStream::TBufferedAudioStream()
	: m_dir("")
	, m_name("")
	, m_fileName("")
	, m_wasRecording(false)
{
    m_rtBufferSlotsQueue = nullptr;
    m_freeBufferSlotsQueue = nullptr;
    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::SyncStatus::OUT_OF_SYNC);

}


TBufferedAudioStream::~TBufferedAudioStream()
{
	PENTERDES;
}

void TBufferedAudioStream::prepare_ring_buffers(nframes_t bufferSize)
{
    Q_ASSERT(m_outputRate > 0);

    uint slotcount = (m_outputRate / bufferSize);
    // Make sure we have enough slots to keep the buffer status logic intact
    if (slotcount < 5) {
        slotcount = 5;
    }

    // only re-create our buffers if slotcount and buffersize are different
    if (m_freeBufferSlotsQueue && m_rtBufferSlotsQueue && m_lastQueuedRTBufferSlot) {
        if (m_lastQueuedRTBufferSlot->get_buffer_size() == bufferSize &&
            m_slotcount == slotcount) {
            return;
        }
    }

    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::QUEUE_ABOUT_TO_BE_DELETED);

    delete_queue_buffers();

    TQueueBufferSlot* slot = nullptr;
    m_slotcount = slotcount;

    m_rtBufferSlotsQueue = new moodycamel::BlockingReaderWriterCircularBuffer<TQueueBufferSlot*>(m_slotcount);
    m_freeBufferSlotsQueue = new moodycamel::BlockingReaderWriterCircularBuffer<TQueueBufferSlot*>(m_slotcount);


    m_bufferSlotDuration = TTimeRef(bufferSize, m_outputRate);
    m_bufferSlotFrames = bufferSize;

    for (size_t i=0; i<m_slotcount;++i) {
        slot = new TQueueBufferSlot(i, m_channelCount, bufferSize);
        bool queued = m_freeBufferSlotsQueue->try_enqueue(slot);
        Q_ASSERT(queued);
    }

    Q_ASSERT(slot);
    // We have to assign m_lastQueuedRTBufferSlot to an existing slot
    m_lastQueuedRTBufferSlot = slot;

    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::SyncStatus::OUT_OF_SYNC);

    // printf("AudioSource::::prepare_ring_buffers: freeBufferSlotsQueue slot count %zu\n", m_freeBufferSlotsQueue->size_approx());
    // printf("AudioSource::::prepare_ring_buffers: rtBufferSlotsQueue slot count %zu\n", m_rtBufferSlotsQueue->size_approx());
}

// Where a seek step's time went, from the decoder's counters before and
// after it: the reads (page cache, medium, untold), the decoding (the calls'
// time without their reads) and the rest (resampling, the ring), one line per
// stream (T71: is a slow seek the medium or the decoder)
static void log_seek(const QString& name, const TAudioIOStats& before, const TAudioIOStats& after, quint64 wallNanoseconds)
{
    const auto transfer = [](TAudioTransfer TAudioPhaseStats::* kind, const TAudioIOStats& stats) {
        TAudioTransfer sum = stats.seek.*kind;
        sum.add(stats.stream.*kind);
        return sum;
    };
    const auto kibibytes = [](quint64 a, quint64 b) { return double(a - b) / 1024.0; };
    const auto milliseconds = [](quint64 a, quint64 b) { return double(a - b) / 1.0e6; };
    const TAudioTransfer cachedBefore = transfer(&TAudioPhaseStats::cached, before), cachedAfter = transfer(&TAudioPhaseStats::cached, after);
    const TAudioTransfer mediumBefore = transfer(&TAudioPhaseStats::medium, before), mediumAfter = transfer(&TAudioPhaseStats::medium, after);
    const TAudioTransfer untoldBefore = transfer(&TAudioPhaseStats::untold, before), untoldAfter = transfer(&TAudioPhaseStats::untold, after);
    const quint64 callsBefore = before.seek.nanoseconds + before.stream.nanoseconds;
    const quint64 callsAfter = after.seek.nanoseconds + after.stream.nanoseconds;
    const quint64 readsNanoseconds = (cachedAfter.nanoseconds - cachedBefore.nanoseconds) + (mediumAfter.nanoseconds - mediumBefore.nanoseconds)
                                     + (untoldAfter.nanoseconds - untoldBefore.nanoseconds);
    const quint64 callsNanoseconds = callsAfter - callsBefore;
    const quint64 decodeNanoseconds = callsNanoseconds > readsNanoseconds ? callsNanoseconds - readsNanoseconds : 0;
    const quint64 restNanoseconds = wallNanoseconds > callsNanoseconds ? wallNanoseconds - callsNanoseconds : 0;
    // Nothing decoded (a stream out of its window): nothing to tell
    if (callsNanoseconds == 0) {
        return;
    }
    // The numbers first: a log line is cut after 256 characters, so only a
    // long file name can be cut
    TLOG_I("diskio", "seek {:.1f} ms = reads {:.1f} (cache {:.0f}K {:.1f}, medium {:.0f}K {:.1f}, untold {:.0f}K {:.1f}) + decode {:.1f} (seek {:.1f}) + rest {:.1f}: {}",
           double(wallNanoseconds) / 1.0e6, double(readsNanoseconds) / 1.0e6,
           kibibytes(cachedAfter.bytes, cachedBefore.bytes), milliseconds(cachedAfter.nanoseconds, cachedBefore.nanoseconds),
           kibibytes(mediumAfter.bytes, mediumBefore.bytes), milliseconds(mediumAfter.nanoseconds, mediumBefore.nanoseconds),
           kibibytes(untoldAfter.bytes, untoldBefore.bytes), milliseconds(untoldAfter.nanoseconds, untoldBefore.nanoseconds),
           double(decodeNanoseconds) / 1.0e6, milliseconds(after.seek.nanoseconds, before.seek.nanoseconds), double(restNanoseconds) / 1.0e6,
           name.toStdString());
}

int TBufferedAudioStream::step(const StepInfo& info)
{
    // One decode buffer per thread that steps (a worker of the scheduler's
    // pool)
    static thread_local TAudioBuffer fileIOBuffer;
    TBufferedAudioStreamStatus& status = get_buffer_status();

    // A refill from before a seek that came while it waited for a thread: the
    // seek step comes next. A seek step is never stale: a song seeks once
    // at a time, and this stream's own seeks run in its steps.
    m_stepGeneration = info.generation;
    if (!info.seek && stale()) {
        return -1;
    }

    if (info.seek) {
        const TAudioIOStats* stats = decoder_stats();
        const TAudioIOStats before = stats ? *stats : TAudioIOStats{};
        const auto start = std::chrono::steady_clock::now();
        if (info.sampleRateChanged) {
            set_output_rate_and_resampler(int(info.outputRate), info.resampler);
            prepare_ring_buffers(info.bufferSize);
        }
        rb_seek_to_transport_location(fileIOBuffer, info.seekLocation);
        // The seek fills part of the ring; the rest at once
        const int seekFill = status.get_fill_status();
        if (!status.out_of_sync() && seekFill <= TIOPlanner::IdleStartThreshold) {
            process_realtime_buffers(fileIOBuffer);
        }
        if (stats) {
            log_seek(get_name(), before, *stats,
                     quint64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count()));
        }
        // Out of its window is no failure: nothing to read there yet
        const int syncStatus = status.get_sync_status();
        if (syncStatus != TBufferedAudioStreamStatus::IN_SYNC && syncStatus != TBufferedAudioStreamStatus::OUT_OF_WINDOW) {
            TLOG_W("diskio", "{}: the seek did not get in sync ({}); the next step tries again",
                   get_name().toStdString(), status.get_readable_sync_status().toStdString());
        }
        return status.out_of_sync() ? -1 : seekFill;
    }

    // Out of sync: seek to where the transport is (a reader whose window the
    // transport entered); otherwise read or write all there is room for
    if (status.out_of_sync()) {
        m_stepGeneration = next_generation();
        rb_seek_to_transport_location(fileIOBuffer, info.transportLocation);
    } else {
        process_realtime_buffers(fileIOBuffer);
    }
    return status.out_of_sync() ? -1 : status.get_fill_status();
}

void TBufferedAudioStream::retire()
{
    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::QUEUE_ABOUT_TO_BE_DELETED);
    delete_queue_buffers();
}

void TBufferedAudioStream::delete_queue_buffers()
{
    Q_ASSERT(m_bufferstatus.get_sync_status() == TBufferedAudioStreamStatus::QUEUE_ABOUT_TO_BE_DELETED);

    TQueueBufferSlot* slot;

    if (m_freeBufferSlotsQueue) {
        while(m_freeBufferSlotsQueue->try_dequeue(slot)) {
            delete slot;
            slot = nullptr;
        }
        Q_ASSERT(m_freeBufferSlotsQueue->size_approx() == 0);
        delete m_freeBufferSlotsQueue;
        m_freeBufferSlotsQueue = nullptr;
    }

    if (m_rtBufferSlotsQueue) {
        while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
            delete slot;
            slot = nullptr;
        }
        Q_ASSERT(m_rtBufferSlotsQueue->size_approx() == 0);
        delete m_rtBufferSlotsQueue;
        m_rtBufferSlotsQueue = nullptr;
    }
}



void TBufferedAudioStream::set_name(const QString& name)
{
	m_name = name;
	if (m_wasRecording) {
		m_shortName = m_name.left(m_name.length() - 20);
	} else {
		m_shortName = m_name;
	}
	m_fileName = m_dir + m_name;
}


void TBufferedAudioStream::set_dir(const QString& dir)
{
	m_dir = dir;
	m_fileName = m_dir + m_name;
}


uint TBufferedAudioStream::get_sample_rate( ) const
{
	return m_rate;
}

void TBufferedAudioStream::set_original_bit_depth( uint bitDepth )
{
	m_origBitDepth = bitDepth;
}

void TBufferedAudioStream::set_created_by_sheet(qint64 id)
{
	m_origSheetId = id;
}

QString TBufferedAudioStream::get_filename( ) const
{
	return m_fileName;
}

QString TBufferedAudioStream::get_dir( ) const
{
	return m_dir;
}

QString TBufferedAudioStream::get_name( ) const
{
	return m_name;
}

uint TBufferedAudioStream::get_bit_depth( ) const
{
	return m_origBitDepth;
}

QString TBufferedAudioStream::get_short_name() const
{
	return m_shortName;
}

// eof
