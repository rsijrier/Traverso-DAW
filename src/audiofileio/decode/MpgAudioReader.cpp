// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "MpgAudioReader.h"

#include "TAudioBuffer.h"

#include <QByteArray>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <mutex>
#include <vector>

// off_t stays out of our code; this asks for mpg123_info2() and the 64 calls
#define MPG123_PORTABLE_API
#include <mpg123.h>

// The junk mpg123 searches through before the first frame. With NO_RESYNC it
// sets the search at the start only
static constexpr long ResyncLimit = 1024 * 1024;
// Without a tag: what may follow the last frame the scan found (tags at the
// end). More is damage, not an end
static constexpr qint64 ScanEndSlack = 64 * 1024;
// Entries of the seek index, at most 80 KB per handle. The default of 1000
// reads about 94 KB per seek on an hour of audio, 10000 about 14 KB
static constexpr long IndexSize = 10000;
static constexpr long PreframesMpeg1 = 12;
static constexpr long PreframesMpeg2 = 90;
// The synthesis filter's delay in samples
static constexpr int64_t DecoderDelay = 529;

// libmpg123's IO on a byte source

static int source_read(void* handle, void* data, size_t bytes, size_t* got)
{
    MpgInput* input = static_cast<MpgInput*>(handle);
    const qint64 read = input->source->read(data, qint64(bytes));
    *got = size_t(std::max<qint64>(read, 0));
    input->furthest = std::max(input->furthest, input->source->position());
    // The failure stays with the source, which the decoder asks
    return read < 0 ? -1 : 0;
}

static int64_t source_seek(void* handle, int64_t offset, int whence)
{
    TByteSource* source = static_cast<MpgInput*>(handle)->source;
    const qint64 position = whence == SEEK_SET ? offset : whence == SEEK_CUR ? source->position() + offset : source->size() + offset;
    return source->seek(position) ? position : -1;
}

// A handle open on input, the same for the probe and the reader; nullptr when
// there is none. Not MPG123_NO_PEEK_END in the probe: it makes the stream
// unseekable, and MPG123_SKIP_ID3V2 then reads through the tag
static mpg123_handle* open_handle(MpgInput& input, int* error)
{
    const QByteArray decoder = qgetenv("TRAVERSO_MPG123_DECODER");
    mpg123_handle* handle = nullptr;
    {
        // Finding the CPU's features is not thread safe on ARM (see the header)
        static std::mutex newMutex;
        const std::lock_guard<std::mutex> lock(newMutex);
        handle = mpg123_new(decoder.isEmpty() ? nullptr : decoder.constData(), error);
    }
    if (!handle) {
        return nullptr;
    }

    // Not MPG123_NO_FRANKENSTEIN: it cuts off files that are concatenated.
    // MPG123_SKIP_ID3V2 skips a tag of any size instead of reading album art
    // into every handle.
    mpg123_param(handle, MPG123_REMOVE_FLAGS, MPG123_AUTO_RESAMPLE | MPG123_GAPLESS, 0.0);
    mpg123_param(handle, MPG123_ADD_FLAGS, MPG123_FORCE_FLOAT | MPG123_QUIET | MPG123_NO_RESYNC | MPG123_SKIP_ID3V2, 0.0);
    mpg123_param(handle, MPG123_RESYNC_LIMIT, ResyncLimit, 0.0);

    input.source->seek(0);
    *error = mpg123_reader64(handle, source_read, source_seek, nullptr);
    if (*error == MPG123_OK && mpg123_open_handle64(handle, &input) != MPG123_OK) {
        // The call only says MPG123_ERR; the handle knows why
        *error = mpg123_errcode(handle);
    }
    if (*error != MPG123_OK) {
        mpg123_delete(handle);
        return nullptr;
    }
    return handle;
}

// What the tag's frame count says, 0 without one. mpg123_length64() gives an
// estimate from the file size then, and no call tells the two apart: only the
// estimate moves with the file size. mpg123 ends a stream at that size and
// leaves an ID3v1 tag out of it, so that is what goes back.
static int64_t tag_samples(mpg123_handle* handle, int64_t fileSize)
{
    mpg123_id3v1* id3v1 = nullptr;
    mpg123_id3(handle, &id3v1, nullptr);
    const int64_t streamSize = id3v1 ? fileSize - 128 : fileSize;

    const int64_t samples = mpg123_length64(handle);
    mpg123_set_filesize64(handle, 2 * streamSize);
    const bool counted = mpg123_length64(handle) == samples;
    mpg123_set_filesize64(handle, streamSize);
    Q_ASSERT(mpg123_length64(handle) == samples);
    return counted ? samples : 0;
}

MpgAudioReader::MpgAudioReader(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
    : TAudioDecoder(fileName, std::move(source), opener)
{
}

std::expected<std::unique_ptr<MpgAudioReader>, TAudioFileError> MpgAudioReader::open(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
{
    std::unique_ptr<MpgAudioReader> decoder(new MpgAudioReader(fileName, std::move(source), opener));
    if (TAudioFileStatus started = decoder->start().and_then([&] { return decoder->scan(); }); !started) {
        return std::unexpected(std::move(started.error()));
    }
    return decoder;
}

TAudioFileStatus MpgAudioReader::start()
{
    m_input.source = &source();

    int error = MPG123_OK;
    m_handle = open_handle(m_input, &error);
    if (!m_handle) {
        return open_failed(QString::fromUtf8(mpg123_plain_strerror(error)));
    }

    long rate = 0;
    int channels = 0;
    int encoding = 0;
    if (mpg123_getformat(m_handle, &rate, &channels, &encoding) != MPG123_OK) {
        return open_failed(QString::fromUtf8(mpg123_strerror(m_handle)));
    }
    if (encoding != MPG123_ENC_FLOAT_32) {
        return open_failed(QStringLiteral("this libmpg123 does not decode to 32 bit float"));
    }

    double unused = 0.0;
    mpg123_getstate(m_handle, MPG123_ENC_DELAY, &m_delay, &unused);
    mpg123_getstate(m_handle, MPG123_ENC_PADDING, &m_padding, &unused);
    m_tagSamples = tag_samples(m_handle, m_input.source->size());
    m_frameSamples = mpg123_spf(m_handle);

    mpg123_frameinfo2 info{};
    mpg123_param(m_handle, MPG123_INDEX_SIZE, IndexSize, 0.0);
    mpg123_info(m_handle, &info);
    mpg123_param(m_handle, MPG123_PREFRAMES, info.version == MPG123_1_0 ? PreframesMpeg1 : PreframesMpeg2, 0.0);

    m_channels = uint(channels);
    m_rate = uint(rate);
    return {};
}

MpgAudioReader::~MpgAudioReader()
{
    if (m_handle) {
        mpg123_delete(m_handle);
    }
}

TAudioFileStatus MpgAudioReader::open_failed(const QString& detail)
{
    std::optional<QString> sourceError = m_input.source->take_error();
    return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, sourceError.value_or(detail)});
}

bool MpgAudioReader::can_decode(TByteSource& source)
{
    MpgInput input{&source};
    int error = MPG123_OK;
    const std::unique_ptr<mpg123_handle, decltype(&mpg123_delete)> handle(open_handle(input, &error), mpg123_delete);
    if (!handle) {
        return false;
    }

    long rate = 0;
    int channels = 0;
    int encoding = 0;
    if (mpg123_getformat(handle.get(), &rate, &channels, &encoding) != MPG123_OK) {
        return false;
    }
    mpg123_frameinfo2 first{};
    if (mpg123_info(handle.get(), &first) != MPG123_OK) {
        return false;
    }

    // The open read a frame already, so n frames walked is n + 1 frames. On
    // random bytes an open alone hits about 1 in 600 to 2500 files of 70 KB;
    // none was seen at 2 frames walked, one at 1 frame and a clean end
    int walked = 0;
    while (walked < 4) {
        const int result = mpg123_framebyframe_next(handle.get());
        if (result == MPG123_DONE) {
            long delay = -1;
            double unused = 0.0;
            mpg123_getstate(handle.get(), MPG123_ENC_DELAY, &delay, &unused);
            return walked >= 2 || (walked == 1 && delay >= 0);
        }
        if (result != MPG123_OK && result != MPG123_NEW_FORMAT) {
            return false;
        }
        mpg123_frameinfo2 info{};
        if (mpg123_info(handle.get(), &info) != MPG123_OK || info.layer != first.layer || info.version != first.version || info.rate != first.rate) {
            return false;
        }
        ++walked;
    }
    return true;
}

TAudioFileStatus MpgAudioReader::scan()
{
    m_input.furthest = 0;
    const int scanned = mpg123_scan(m_handle);
    // mpg123_scan() takes a failed read for the end of the file: never a
    // silently short length
    if (std::optional<QString> sourceError = m_input.source->take_error()) {
        return open_failed(*sourceError);
    }
    if (scanned != MPG123_OK) {
        return open_failed(QString::fromUtf8(mpg123_strerror(m_handle)));
    }
    m_scannedSamples = mpg123_length64(m_handle);

    const bool gapless = m_delay >= 0 && m_padding >= 0;
    int64_t frames = m_scannedSamples;
    if (m_tagSamples > 0 && gapless) {
        // The tag goes first when it says more than a frame over what the scan
        // found, a file cut short: the audio stays in place and the end is an
        // error. One frame over is iTunes, which counts its Info frame. When
        // the tag says less (concatenated files) nothing is cut
        if (m_tagSamples > m_scannedSamples + m_frameSamples) {
            frames = m_tagSamples;
        }
    } else if (const qint64 size = m_input.source->size(); size - m_input.furthest > ScanEndSlack) {
        return open_failed(QStringLiteral("the audio stops at byte %1 of %2").arg(m_input.furthest).arg(size));
    }
    // Also without a frame count: delay and padding place the audio
    if (gapless) {
        m_skip = m_delay + DecoderDelay;
        frames -= m_delay + std::max<int64_t>(m_padding, DecoderDelay);
    }

    if (frames <= 0 || frames > std::numeric_limits<nframes_t>::max()) {
        return open_failed(QStringLiteral("%1 frames of audio").arg(frames));
    }
    m_frames = nframes_t(frames);

    if (TAudioFileStatus sought = seek(0); !sought) {
        return open_failed(sought.error().detail);
    }
    return has_audio();
}

TAudioFileStatus MpgAudioReader::seek_frame(nframes_t frame)
{
    Q_ASSERT(frame < m_frames);
    // mpg123 turns a position into frames with the size of the frame it is at:
    // past a change of rate no seek can be trusted
    if (m_formatChanged) {
        return seek_failed(frame, QStringLiteral("the format changes inside the file: no seek until it is opened again"));
    }
    const int64_t position = int64_t(frame) + m_skip;
    if (position >= m_scannedSamples) {
        return seek_failed(frame, QStringLiteral("frame %1 lies past the audio in the file").arg(frame));
    }
    if (mpg123_seek64(m_handle, position, SEEK_SET) != position) {
        return seek_failed(frame, QString::fromUtf8(mpg123_strerror(m_handle)));
    }
    // mpg123's seek takes a failed read for the end of the stream and lands
    if (std::optional<QString> sourceError = m_input.source->take_error()) {
        return seek_failed(frame, *sourceError);
    }
    return seek_landed(frame);
}

bool MpgAudioReader::same_format()
{
    long rate = 0;
    int channels = 0;
    int encoding = 0;
    return mpg123_getformat(m_handle, &rate, &channels, &encoding) == MPG123_OK && uint(rate) == m_rate && uint(channels) == m_channels;
}

// libmpg123's interleaved frames before they go to the channels: one per
// thread, used within one read only, grown to the largest read on the thread
static float* interleaved_scratch(size_t samples)
{
    static thread_local std::vector<float> scratch;
    if (scratch.size() < samples) {
        scratch.resize(samples);
    }
    return scratch.data();
}

TAudioFileResult MpgAudioReader::read_frames(TAudioBuffer& fileIOBuffer, nframes_t count)
{
    if (count == 0) {
        return {};
    }
    fileIOBuffer.check_capacity(count, m_channels);

    const nframes_t wanted = !m_error ? std::min(count, m_frames - m_frame) : 0;
    float* readBuffer = interleaved_scratch(size_t(wanted) * m_channels);

    const size_t frameBytes = m_channels * sizeof(float);
    const size_t bytes = wanted * frameBytes;
    size_t have = 0;
    int result = MPG123_OK;
    while (have < bytes && result == MPG123_OK) {
        size_t done = 0;
        result = mpg123_read(m_handle, reinterpret_cast<char*>(readBuffer) + have, bytes - have, &done);
        have += done;
        // The format at the open again goes on, another one is damage
        if (result == MPG123_NEW_FORMAT && same_format()) {
            result = MPG123_OK;
        }
    }
    const nframes_t readFrames = nframes_t(have / frameBytes);

    for (uint channel = 0; channel < m_channels; channel++) {
        audio_sample_t* destination = fileIOBuffer.get_channel_buffer(channel).get_data(readFrames);
        for (nframes_t frame = 0; frame < readFrames; frame++) {
            destination[frame] = readBuffer[frame * m_channels + channel];
        }
    }

    QString detail;
    if (result == MPG123_NEW_FORMAT) {
        m_formatChanged = true;
        detail = QStringLiteral("the format changes at frame %1").arg(m_frame + readFrames);
    } else if (result != MPG123_OK && result != MPG123_DONE) {
        detail = QString::fromUtf8(mpg123_strerror(m_handle));
    }
    return read_done(fileIOBuffer, readFrames, count, detail);
}
