/*
Copyright (C) 2006-2026 Ben Levitt, Remon Sijrier

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

#include "TByteSource.h"
#include "TAudioFileError.h"
#include "TAudioBuffer.h"
#include "TAudioIOStats.h"
#include "TResamplerChoice.h"
#include "TTimeRef.h"
#include "TTypes.h"

#include <QString>

#include <expected>
#include <memory>
#include <optional>

class TAudioDecoder;
class TAudioResampler;

/**
 * @brief The audio of one file: the one way to read an audio file in
 * Traverso (TAudioWriter is the one way to write one).
 *
 * It opens the file with the decoder for its format (libsndfile, WavPack,
 * MP3, AAC, or a scenario's test signal) and gives the file's facts:
 * channels, rate, frames, length. The audio comes two ways:
 * - read(): at the output rate, from a location in time. With the output
 *   rate the file's own, the file as it is; with another, through a
 *   TAudioResampler for all channels, sample exact: every output frame is
 *   where it belongs on the file's timeline (test_resample_reader).
 * - read_file_frames(): the file's own frames, at its own rate, whatever the
 *   output rate: for what works on the file's grid (peaks, a waveform).
 *
 * One reader per use: a stream, a waveform and a peak build each open their
 * own. Both ways on one reader read right, but a read_file_frames() moves
 * the decoder under the output, so the next read() seeks and its resampler
 * starts anew.
 *
 * Both keep the contract of TAudioDecoder: count frames of every channel are
 * filled, what the file gives and zeros for the rest, also past the end of
 * the file; the caller never silences before a read. Fewer frames than asked
 * come only at the end of the file or after a decoder error, on both ways
 * and at any output rate; the next read seeks then, which may clear the
 * error (a damaged part of a file skipped).
 *
 * Positions come in as time (TTimeRef) or as the file's frames; the frames
 * at the output rate stay inside. Not for the audio thread: file IO, and the
 * buffers may grow. One thread at a time: the owner serializes the calls
 * (read(), read_file_frames(), set_output_rate_and_resampler()); the IO
 * scheduler does so for the streams.
 *
 * A reader exists only for a file it reads: open() gives one, or why there
 * is none. Its facts are the file's, its reads read.
 */
class TAudioReader
{
public:
    // Whether a decoder reads fileName: a probe, nothing is kept or logged
    static bool can_read(const QString& fileName);
    // A reader of fileName, at the file's own rate until
    // set_output_rate_and_resampler(); or why there is none (NotFound,
    // CannotOpen). opener gives the file's bytes (tests).
    [[nodiscard]] static std::expected<std::unique_ptr<TAudioReader>, TAudioFileError> open(const QString& fileName, const TByteSourceOpener& opener = {});

    ~TAudioReader();

    // The file, as its decoder knows it
    uint channel_count() const;
    uint file_rate() const;
    nframes_t file_frame_count() const;
    TTimeRef length() const;
    QString decoder_type() const;
    // What the decoder read and decoded since it opened: for this reader's
    // thread
    const TAudioIOStats& io_stats() const;

    // The output rate of read(), and the resampler for it when it is not the
    // file's rate (a backend and one of its qualities). The reader has no
    // position at the new rate: the next read() gives it one. Until it is
    // called, the output rate is the file's. rate > 0. A resampler that does
    // not start is an error (CannotOpen), and the reader stays as it was.
    [[nodiscard]] TAudioFileStatus set_output_rate_and_resampler(uint rate, TResamplerChoice resampler);
    uint output_rate() const { return m_outputRate; }
    // The frames the file gives at the output rate: the first output frame at
    // or after its length, where read() ends
    nframes_t output_frame_count() const { return m_outputFrames; }

    // count frames at the output rate into buffer, from the output frame
    // nearest to location (frame n sits at n / rate,
    // TTimeRef::frame_nearest()): the frames that came from the file, and
    // the error of this read when it failed (a caller that only needs the
    // count writes .frames). A seek only when the reader is elsewhere or its
    // decoder is in its error state: reads at locations whole frames apart,
    // each where the last one ended, are one read, whatever fraction of a
    // frame the first location has. At or past the end: zeros and 0, no
    // seek, no error.
    TAudioFileResult read(TAudioBuffer& buffer, const TTimeRef& location, nframes_t count);
    // count frames of the file at its own rate into buffer, from its frame
    // firstFrame: the frames that came from the file, and the error of this
    // read when it failed
    TAudioFileResult read_file_frames(TAudioBuffer& buffer, nframes_t firstFrame, nframes_t count);
    // A seek starts from nothing: no position, the next read() seeks the
    // decoder, also to the frame it is at, and the resampler keeps nothing
    void forget_position();

private:
    explicit TAudioReader(std::unique_ptr<TAudioDecoder> decoder);

    // The reader at output frame frame: the decoder sought there, through
    // the resampler from the last frame of both rates before it. False:
    // nothing to read there (a rounding onto the file's end); the error of a
    // seek that failed
    std::expected<bool, TAudioFileError> reposition(nframes_t frame);
    // The resampler and the frames to drop forget the position read before
    void reset();
    // count resampled frames from the reader's position into buffer; what
    // came from the file, zeros after it, and the error of the decoder's
    // reads
    TAudioFileResult read_resampled(TAudioBuffer& buffer, nframes_t count);
    // count output frames into out from frame 0, reading the file as far as
    // that takes; fewer only at the end of the file or after a decoder
    // error (the resampler flushed at both), with that error
    TAudioFileResult resample_into(TAudioBuffer& out, nframes_t count);
    // The file frames that cover outputFrames output frames: one read of
    // the decoder for them
    nframes_t file_frames_for(nframes_t outputFrames) const;

    std::unique_ptr<TAudioDecoder> m_decoder;
    // Only when the output rate is not the file's
    std::unique_ptr<TAudioResampler> m_resampler;
    // What the decoder reads for the resampler
    TAudioBuffer m_decodeBuffer;

    uint m_outputRate{0};
    nframes_t m_outputFrames{0};
    // The position of read(), in output frames. None after a new output
    // rate, a read of the file's own frames (the decoder moved), a seek that
    // failed or a read at or past the end: the next read() seeks.
    std::optional<nframes_t> m_frame{0};
    // After a seek: output frames to drop before the frame asked for
    nframes_t m_dropFrames{0};
};
