/*
Copyright (C) 2007 - 2026 Ben Levitt, Remon Sijrier

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


#include "TAudioReader.h"
#include "TAudioDecoder.h"
#include "TAudioResampler.h"

#include <algorithm>

bool TAudioReader::can_read(const QString& fileName)
{
    return can_decode_audio(fileName);
}

std::expected<std::unique_ptr<TAudioReader>, TAudioFileError> TAudioReader::open(const QString& fileName, const TAudioSourceOpener& opener)
{
    std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError> decoder = open_audio_decoder(fileName, opener);
    if (!decoder) {
        return std::unexpected(std::move(decoder.error()));
    }
    return std::unique_ptr<TAudioReader>(new TAudioReader(std::move(*decoder)));
}

TAudioReader::TAudioReader(std::unique_ptr<TAudioDecoder> decoder)
    : m_decoder(std::move(decoder))
    , m_outputRate(m_decoder->rate())
    , m_outputFrames(m_decoder->frame_count())
{
}

TAudioReader::~TAudioReader() = default;

uint TAudioReader::channel_count() const
{
    return m_decoder->channel_count();
}

uint TAudioReader::file_rate() const
{
    return m_decoder->rate();
}

nframes_t TAudioReader::file_frame_count() const
{
    return m_decoder->frame_count();
}

TTimeRef TAudioReader::length() const
{
    return m_decoder->length();
}

const TAudioIOStats& TAudioReader::io_stats() const
{
    return m_decoder->stats();
}

QString TAudioReader::decoder_type() const
{
    return m_decoder->decoder_type();
}

TAudioFileStatus TAudioReader::set_output_rate_and_resampler(uint rate, TResamplerChoice resampler)
{
    Q_ASSERT(rate > 0);

    // Another ratio is another resampler (soxr cannot change it once it
    // ran). Made first: one that does not start leaves the reader as it was.
    std::unique_ptr<TAudioResampler> made;
    if (rate != file_rate()) {
        made = std::make_unique<TAudioResampler>(resampler, double(rate) / file_rate(), channel_count());
        if (!made->valid()) {
            return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, QStringLiteral("the %1 resampler did not start")
                                                                                          .arg(TAudioResampler::backend_name(resampler.backend))});
        }
    }

    m_outputRate = rate;
    // The length is the file's, at any output rate; the frames the file
    // gives at the output rate end where it ends
    m_outputFrames = rate == file_rate() ? m_decoder->frame_count()
                                         : TTimeRef::to_nframes(TTimeRef::frame_at_or_after(m_decoder->length(), rate));

    m_resampler = std::move(made);
    m_dropFrames = 0;

    // A frame at the old rate is another time at this one: no position, the
    // next read() seeks (its owner seeks after a change of rate anyway,
    // TBufferedAudioStream::step())
    m_frame.reset();
    return {};
}

// Forgets what the resampler holds of the frames fed so far (the filter
// history the next output frames would be computed from, and input kept for
// the next feed) and the frames still to drop after a seek: all of it belongs
// to the position read before, and the next read is from another one
void TAudioReader::reset()
{
    if (m_resampler) {
        m_resampler->reset();
    }
    m_dropFrames = 0;
}

std::expected<bool, TAudioFileError> TAudioReader::reposition(nframes_t frame)
{
    // Where a seek fails the decoder may be anywhere: no position then
    m_frame.reset();
    if (!m_resampler) {
        if (TAudioFileStatus sought = m_decoder->seek(frame); !sought) {
            return std::unexpected(std::move(sought.error()));
        }
        m_frame = frame;
        return true;
    }

    // The resampler's filter history and kept input are from the position
    // read before: fed on, they would put that position's tail (a click, the
    // length of the filter) and its kept frames at the new position. Before
    // the drop is set below, since the reset clears it.
    reset();

    // A position in output frames is mostly no whole frame of the file, and
    // a seek to the nearest file frame would shift the output by a fraction
    // of a frame. The file is sought to the last frame before frame where a
    // frame of both rates coincide (every 147 frames of 44.1 kHz and 160 of
    // 48 kHz); the next read drops the output frames from there to frame.
    const TTimeRef position(frame, m_outputRate);
    const TTimeRef common = TTimeRef::common_frame_at_or_before(position, file_rate(), m_outputRate);
    const nframes_t fileFrame = TTimeRef::to_nframes(TTimeRef::frame_at_or_before(common, file_rate()));
    // Below the file's frame count for every output rate that divides the
    // universal rate; at one that does not, the last output frame can round
    // onto the file's end: nothing to read there, not an error (a decoder
    // seeks below its frame count only)
    if (fileFrame >= file_frame_count()) {
        return false;
    }
    if (TAudioFileStatus sought = m_decoder->seek(fileFrame); !sought) {
        return std::unexpected(std::move(sought.error()));
    }
    m_dropFrames = TTimeRef::to_nframes(TTimeRef::frame_at_or_before(position - common, m_outputRate));
    m_frame = frame;
    return true;
}

void TAudioReader::forget_position()
{
    m_frame.reset();
    reset();
}

TAudioFileResult TAudioReader::read(TAudioBuffer& buffer, const TTimeRef& location, nframes_t count)
{
    if (count == 0) {
        return {};
    }
    // The reader writes into the buffer, so it makes room in it
    buffer.check_capacity(count, channel_count());

    // Nothing to read: zeros, as past the end of the file
    const auto nothing = [&](std::optional<TAudioFileError> error) -> TAudioFileResult {
        buffer.silence(0, count, channel_count());
        return {0, std::move(error)};
    };

    // The output frame nearest to location; before the file: frame 0. The
    // one rounding of a read: it moves with the location, so a read at the
    // location where the last one ended goes on from there, no seek
    const nframes_t first = TTimeRef::to_nframes(std::max<qint64>(0, TTimeRef::frame_nearest(location, m_outputRate)));
    // At or past the end: the decoder is not asked (it seeks only below its
    // frame count), no error. No position: the next read seeks.
    if (first >= m_outputFrames) {
        m_frame.reset();
        return nothing(std::nullopt);
    }
    // Elsewhere, no position at all, or a decoder in its error state (after
    // a read that failed, the reader's position may still be right): a seek
    const bool readsFromFirst = m_frame == first && !m_decoder->error();
    if (!readsFromFirst) {
        std::expected<bool, TAudioFileError> positioned = reposition(first);
        if (!positioned) {
            return nothing(std::move(positioned.error()));
        }
        if (!*positioned) {
            return nothing(std::nullopt);
        }
    }

    TAudioFileResult result = m_resampler ? read_resampled(buffer, count) : m_decoder->read(buffer, count);
    m_frame = first + result.frames;
    return result;
}

TAudioFileResult TAudioReader::read_file_frames(TAudioBuffer& buffer, nframes_t firstFrame, nframes_t count)
{
    if (count == 0) {
        return {};
    }
    // The decoder moves under the output: no position, the next read() seeks
    m_frame.reset();
    return m_decoder->read_from(buffer, firstFrame, count);
}

TAudioFileResult TAudioReader::read_resampled(TAudioBuffer& buffer, nframes_t count)
{
    // Not past the frames the file gives at the output rate (read() asks
    // below them only)
    const nframes_t frames = std::min(count, m_outputFrames - *m_frame);

    // After a seek: the output frames between the common frame the file was
    // sought to and the frame asked for (reposition()), in pieces the buffer
    // holds; the output asked for writes over them. The decoder reads ahead
    // of the output (the resampler's filter), so an error met there may lie
    // past the frames asked for: they still come, flushed (resample_into()),
    // and the error is this read's.
    std::optional<TAudioFileError> dropError;
    while (m_dropFrames > 0) {
        const nframes_t toDrop = std::min(m_dropFrames, frames);
        TAudioFileResult dropped = resample_into(buffer, toDrop);
        m_dropFrames = dropped.frames < toDrop ? 0 : m_dropFrames - toDrop;
        if (dropped.error && !dropError) {
            dropError = std::move(dropped.error);
        }
    }

    // Zeros only past what the resampler gives: padding a short read
    // anywhere else would shift all that follows. Short at the end of the
    // file is the resampler's rounding, those frames count as read; short
    // after a decoder error they do not, so the direct and the resampled
    // path report an error the same way. The decoder reads ahead, so a read
    // can also give all its frames and the error it met beyond them.
    TAudioFileResult result = resample_into(buffer, frames);
    buffer.silence(result.frames, count - result.frames, channel_count());
    if (!result.error) {
        result.error = std::move(dropError);
    }
    if (!result.error) {
        result.frames = frames;
    }
    return result;
}

nframes_t TAudioReader::file_frames_for(nframes_t outputFrames) const
{
    return TTimeRef::to_nframes(TTimeRef::frames_covering(outputFrames, m_outputRate, file_rate()));
}

TAudioFileResult TAudioReader::resample_into(TAudioBuffer& out, nframes_t count)
{
    // The resampler gives what its input allows: after a seek it holds back
    // the length of its filter, and soxr releases its output in blocks of its
    // own. So it is fed until it gave all that was asked for, reading as much
    // of the file as that takes; at the end of the file, or where the decoder
    // gives no more (its error state), it is flushed, also over several
    // reads when they ask for less than it holds: the frames before an error
    // come out as those before the end do.
    TAudioFileResult result;
    while (result.frames < count) {
        if (m_decoder->at_end() || m_decoder->error()) {
            result.frames += m_resampler->flush(out.channel_buffers(), result.frames, count - result.frames);
            break;
        }
        // The decoder makes room in its buffer and leaves no stale frames in
        // it. No frames only at the end or in the error state (the
        // decoder's contract): the loop flushes then.
        auto [inputFrames, error] = m_decoder->read(m_decodeBuffer, file_frames_for(count - result.frames));
        if (error) {
            result.error = std::move(error);
        }
        if (inputFrames > 0) {
            result.frames += m_resampler->process(m_decodeBuffer.channel_buffers(), inputFrames, out.channel_buffers(), result.frames, count - result.frames);
        }
    }
    return result;
}
