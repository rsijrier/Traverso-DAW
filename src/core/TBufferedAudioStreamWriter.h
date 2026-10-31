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

#include "TBufferedAudioStream.h"

#include <memory>
#include <vector>
#include "TProcessContext.h"

class TExportSpecification;
class TPeakStream;
class TAudioWriter;
class AudioBus;
class TAudioBuffer;

class TBufferedAudioStreamWriter : public TBufferedAudioStream
{
    Q_OBJECT

public :
    TBufferedAudioStreamWriter(const QString& exportDir, const QString &exportFileName);
    ~TBufferedAudioStreamWriter();

    nframes_t ringbuffer_write(TProcessContext &context) T_REALTIME_SAFE;
    int rb_file_write(TQueueBufferSlot* slot, TAudioBuffer& fileIOBuffer);

    TBufferedAudioStreamStatus& get_buffer_status() final;
    // The peaks of this recording, streamed to its peak file as level 0
    std::shared_ptr<TPeakStream> get_peaks() const { return m_peaks; }

    // The file a recording, an import or a merge writes into (not an export
    // of the project: TExportSpecification::prepare_export()); -1 when it
    // cannot be opened
    // inputRate: the rate the written frames come in at (the device's for a
    // recording); the file is written at the specification's rate
    int prepare_file(TExportSpecification* specification, uint inputRate);
    // All written: the file closed (the resampler's rest, the final header),
    // its peaks finished; fileFinished() follows
    void finish_file();

    void set_process_peaks(bool process);
    void set_recording(bool rec);
    // Before add_stream(): the first frames are not written
    // (latency compensation, TRecordingLatency)
    void set_frames_to_drop(qint64 frames) { m_framesToDrop = frames; }

    bool is_recording() const { return m_isRecording; }
    // The frames in the file, after the dropped ones. Written by the
    // writer's steps; read after fileFinished() (queued), when the writing
    // is over
    qint64 get_written_frames() const { return m_writtenFrames; }

private:
    std::unique_ptr<TAudioWriter>           m_audioWriter;
    std::shared_ptr<TPeakStream>                 m_peaks;
    std::vector<const float*>                       m_peakChannels;

    nframes_t       m_sampleRate;
    bool            m_processPeaks;
    bool            m_isRecording;
    // A recording: its peaks are read while they are written, and
    // post-processed in DiskIO's idle time (an import does it at once)
    bool            m_peaksAreRead{false};
    bool            m_fileFinished{false};
    qint64          m_framesToDrop{0};   // the writer's steps once it is added
    qint64          m_writtenFrames{0};

    TQueueBufferSlot* dequeue_from_free_queue(TProcessContext &context) T_REALTIME_SAFE;

    void process_realtime_buffers(TAudioBuffer &fileIOBuffer) final;
    void rb_seek_to_transport_location(TAudioBuffer&, const TTimeRef &/*transportLocation*/) final {}
    void set_output_rate_and_resampler(int /*outputRate*/, const Resampler& /*resampler*/) final {}
    // Something to write (it is recorded audio: as soon as it is there), or
    // the recording stopped and the file is not finished yet
    TIOStream::State disk_state(const TTimeRef& transportLocation) final;

signals:
    void fileFinished();
};
