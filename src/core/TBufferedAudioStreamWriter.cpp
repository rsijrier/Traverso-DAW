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

#include "TBufferedAudioStreamWriter.h"
#include "TExportSpecification.h"
#include "AudioBus.h"
#include "TAudioDevice.h"
#include "TAudioWriter.h"
#include "TLog.h"
#include "TPeakCache.h"
#include "TPeakPyramid.h"
#include "TPeakStream.h"
#include "TRecordingLatency.h"

#include <cstring>
#include "TWaveform.h"
#include "TQueueBufferSlot.h"
#include "TAudioBuffer.h"
#include "Debugger.h"

TBufferedAudioStreamWriter::TBufferedAudioStreamWriter(const QString& exportDir, const QString &exportFileName)
    : TBufferedAudioStream(exportDir, exportFileName)
{
    m_audioWriter = nullptr;
    m_channelCount = 0;
    m_isRecording = false;
    m_sampleRate = 0;
    m_processPeaks = false;
}

TBufferedAudioStreamWriter::~TBufferedAudioStreamWriter()
{
    PENTERDES;
    // A stream that goes before its file was finished (a project closed
    // between the end of a recording and its last disk step) closes its
    // writer here: TAudioWriter requires close(), and the file keeps what
    // was written
    if (m_audioWriter) {
        if (TAudioFileStatus closed = m_audioWriter->close(); !closed) {
            TLOG_W("diskio", "closing {} failed: it may not hold all that was written ({})", m_fileName.toStdString(),
                   closed.error().detail.toStdString());
        }
    }
}

int TBufferedAudioStreamWriter::prepare_file(TExportSpecification *specification, uint inputRate)
{
    PENTER;
    Q_ASSERT(specification->is_valid() == 1);

    m_outputRate = specification->get_sample_rate();
    m_channelCount = specification->get_channel_count();
    m_sampleRate = inputRate;

    set_name(get_name() + specification->get_file_extension());
    specification->print_export_data();

    std::expected<std::unique_ptr<TAudioWriter>, TAudioFileError> writer = TAudioWriter::open(specification->get_encode_settings(), m_fileName,
                                                                                            m_sampleRate);
    if (!writer) {
        TLOG_W("audiofile", "{}: cannot write it ({})", m_fileName.toStdString(), writer.error().detail.toStdString());
        return -1;
    }
    m_audioWriter = std::move(*writer);

    return 0;
}

void TBufferedAudioStreamWriter::finish_file()
{
    if (m_audioWriter) {
        // The resampler's rest and the final header: a file whose close
        // fails may not hold all that was written
        if (TAudioFileStatus closed = m_audioWriter->close(); !closed) {
            TLOG_W("diskio", "closing {} failed: it may not hold all that was written ({})", m_fileName.toStdString(),
                   closed.error().detail.toStdString());
        }
        m_audioWriter.reset();
    }

    // Level 0 of the peaks is on disk already but for its last block; the
    // header follows once the audio file is complete (its size and time are
    // part of the peak file's validation). A recording's peaks are
    // post-processed in DiskIO's idle time (TAudioClip::finish_write_source());
    // an import or a merge does it here, nobody reads its peaks meanwhile.
    if (m_peaks) {
        const bool failedBefore = m_peaks->write_failed();   // said so then
        QString error;
        if (m_peaks->finish(m_fileName, &error) != TPeakFile::Status::Ok) {
            if (!failedBefore) {
                TLOG_W("peaks", "finishing {} failed: {}", m_peaks->peak_path().toStdString(), error.toStdString());
            }
        } else if (!m_peaksAreRead) {
            while (m_peaks->post_process()) {
            }
            if (m_peaks->published()) {
                TPeakCache::touch(m_peaks->peak_path());
                TPeakCache::evict_later();
            } else {
                TLOG_W("peaks", "post-processing {} failed: {}", m_peaks->peak_path().toStdString(), m_peaks->error().toStdString());
            }
        }
    }

    m_fileFinished = true;
    emit fileFinished();
}

int TBufferedAudioStreamWriter::rb_file_write(TQueueBufferSlot* slot, TAudioBuffer& fileIOBuffer)
{
    nframes_t nframes = slot->get_buffer_size();

    // The first frames of a recording came in before anything could be
    // heard: they are dropped, audio and peaks alike, so the file starts with
    // the frame that belongs to the take's position
    const nframes_t drop = nframes_t(TRecordingLatency::frames_to_drop(m_framesToDrop, nframes));
    if (drop == nframes) {
        return int(nframes);
    }

    fileIOBuffer.check_capacity(nframes, m_channelCount);

    for (uint chan = 0; chan < m_channelCount; ++chan) {
        TChannelBuffer& targetChannelBuffer = fileIOBuffer.get_channel_buffer(chan);
        slot->read_buffer(targetChannelBuffer, chan, nframes);
    }

    // Once per take: the buffer the dropped frames end in
    if (drop > 0) {
        for (uint chan = 0; chan < m_channelCount; ++chan) {
            audio_sample_t* data = fileIOBuffer.get_channel_buffer(chan).get_data(nframes);
            std::memmove(data, data + drop, size_t(nframes - drop) * sizeof(audio_sample_t));
        }
        nframes -= drop;
    }

    if (m_peaks && !m_peaks->write_failed()) {
        for (uint chan = 0; chan < m_channelCount; ++chan) {
            m_peakChannels[chan] = fileIOBuffer.get_channel_buffer(chan).get_data(nframes);
        }
        m_peaks->append(m_peakChannels.data(), nframes);
        if (m_peaks->write_failed()) {
            // Once: the peaks stop, the recording goes on
            TLOG_W("peaks", "writing {} failed, it is built from the audio later: {}", m_peaks->peak_path().toStdString(),
                   m_peaks->error().toStdString());
        }
    }

    // The frames taken, at the stream's rate: all of them, unless the file
    // could not take them (TAudioWriter::write())
    const auto [taken, error] = m_audioWriter->write(fileIOBuffer, nframes);
    m_writtenFrames += taken;

    if (error) {
        TLOG_E("diskio", "{}: writing failed, {} of {} frames taken ({})", m_fileName.toStdString(), taken, nframes, error->detail.toStdString());
        return 0;
    }

    return nframes;
}

nframes_t TBufferedAudioStreamWriter::ringbuffer_write(TProcessContext &context) T_REALTIME_SAFE
{
    Q_ASSERT(m_rtBufferSlotsQueue);
    Q_ASSERT(m_freeBufferSlotsQueue);

    nframes_t nframes = context.get_nframes_to_process();
    AudioBus* bus = context.get_ringbuffer_write_bus();
    Q_ASSERT(bus);
    Q_ASSERT(bus->get_channel_count() == m_channelCount);

    TQueueBufferSlot* slot = dequeue_from_free_queue(context);
    if (slot) {
        slot->update_location_and_offset(context.get_start_location(), TTimeRef());
        for (uint chan = 0; chan < m_channelCount; ++chan) {
            slot->write_channel(chan, bus->get_buffer(chan), 0, nframes);
        }

        // Real-time safe by contract: moodycamel's try_enqueue does not
        // allocate or wait, it wakes the disk thread's semaphore when it sleeps
        bool enqueued = false;
        T_REALTIME_SAFE_BY_CONTRACT(enqueued = m_rtBufferSlotsQueue->try_enqueue(slot);)
        if (!enqueued) {
            TLOG_RT_W("diskio", "ring write: the queue to the disk thread is full, a buffer of the recording is lost");
            return 0;
        }

        return slot->get_buffer_size();
    }

    return 0;
}

TQueueBufferSlot* TBufferedAudioStreamWriter::dequeue_from_free_queue(TProcessContext &context) T_REALTIME_SAFE
{
    TQueueBufferSlot* slot = nullptr;

    if (context.get_is_real_time()) {
        // Real-time safe by contract: moodycamel's try_dequeue does not wait
        T_REALTIME_SAFE_BY_CONTRACT(m_freeBufferSlotsQueue->try_dequeue(slot);)
    } else {
        const trav_time_t startTime = TTimeRef::get_nanoseconds_since_epoch();
        // Freewheeling has no deadline: the wait for the disk thread is by
        // design (doc/design/realtime-audio-path.md §5)
        T_BLOCKING_BY_DESIGN(m_freeBufferSlotsQueue->wait_dequeue(slot);)
        const trav_time_t endTime = TTimeRef::get_nanoseconds_since_epoch();
        context.add_ringbuffer_read_wait_time(endTime - startTime);
    }

    return slot;
}

void TBufferedAudioStreamWriter::process_realtime_buffers(TAudioBuffer &fileIOBuffer)
{
    if (m_fileFinished) {
        return;
    }
    Q_ASSERT(m_audioWriter);

    TQueueBufferSlot* slot = nullptr;
    while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
        rb_file_write(slot, fileIOBuffer);
        m_freeBufferSlotsQueue->try_enqueue(slot);
    }

    if (!m_isRecording) {
        finish_file();
    }
}

TIOStream::State TBufferedAudioStreamWriter::disk_state(const TTimeRef& /*transportLocation*/)
{
    // The fill is the free part: the fullest queue goes first
    const bool work = m_rtBufferSlotsQueue->size_approx() > 0 || (!m_isRecording && !m_fileFinished);
    return TIOStream::State{work, false, get_buffer_status().get_fill_status()};
}

void TBufferedAudioStreamWriter::set_process_peaks(bool process)
{
    m_processPeaks = process;

    if (!m_processPeaks) {
        return;
    }

    Q_ASSERT(!m_peaks);
    Q_ASSERT(m_channelCount > 0);
    m_peaks = std::make_shared<TPeakStream>(m_sampleRate, int(m_channelCount), TWaveform::peak_path(this));
    QString error;
    if (m_peaks->open(&error) != TPeakFile::Status::Ok) {
        // No waveform meanwhile; built from the audio afterwards
        TLOG_W("peaks", "cannot write {}: {}", m_peaks->peak_path().toStdString(), error.toStdString());
    }
    m_peakChannels.assign(m_channelCount, nullptr);
}

void TBufferedAudioStreamWriter::set_recording(bool rec)
{
    m_isRecording = rec;
    if (rec) {
        m_peaksAreRead = true;
    }
}

TBufferedAudioStreamStatus& TBufferedAudioStreamWriter::get_buffer_status()
{
    m_bufferstatus.set_fill_status((m_freeBufferSlotsQueue->size_approx() * 100) / m_slotcount);

    if (!m_isRecording) {
        m_bufferstatus.set_fill_status(0);
    }
    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::IN_SYNC);
    return m_bufferstatus;
}
