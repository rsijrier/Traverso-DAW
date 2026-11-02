// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "FFmpegAudioReader.h"

#include "TAudioBuffer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/common.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
}

#include <algorithm>
#include <cstring>
#include <limits>

// Decoded before the frame a seek goes to: the decoder's overlap, and SBR's
static constexpr int64_t PreRollFrames = 8192;
static constexpr int IoBufferBytes = 32768;
// Packets to look at for the first frame of a file that does not decode at once
static constexpr int FirstFramePackets = 16;

static int source_read(void* opaque, uint8_t* data, int bytes)
{
    const int64_t read = static_cast<TByteSource*>(opaque)->read(data, bytes);
    return read == 0 ? AVERROR_EOF : read < 0 ? AVERROR(EIO) : int(read);
}

static int64_t source_seek(void* opaque, int64_t offset, int whence)
{
    TByteSource* source = static_cast<TByteSource*>(opaque);
    if (whence & AVSEEK_SIZE) {
        return source->size();
    }
    whence &= ~AVSEEK_FORCE;
    const int64_t position = whence == SEEK_SET ? offset : whence == SEEK_CUR ? source->position() + offset : source->size() + offset;
    return source->seek(position) ? position : AVERROR(EIO);
}

static QString words_of(int error)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error, text, sizeof(text));
    return QString::fromUtf8(text);
}

static TAudioFileStatus open_error(const QString& detail)
{
    return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, detail});
}

// An AVFormatContext on source, freed with close_format()
static int open_format(TByteSource& source, AVIOContext*& io, AVFormatContext*& format)
{
    // FFmpeg's log is its own, on stderr: every reason comes back by a return value
    av_log_set_level(AV_LOG_QUIET);
    auto* buffer = static_cast<unsigned char*>(av_malloc(IoBufferBytes));
    if (!buffer) {
        return AVERROR(ENOMEM);
    }
    io = avio_alloc_context(buffer, IoBufferBytes, 0, &source, source_read, nullptr, source_seek);
    format = avformat_alloc_context();
    if (!io || !format) {
        return AVERROR(ENOMEM);
    }
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    source.seek(0);
    return avformat_open_input(&format, nullptr, nullptr, nullptr);
}

static void close_format(AVIOContext*& io, AVFormatContext*& format)
{
    avformat_close_input(&format);
    if (io) {
        av_freep(&io->buffer);
        avio_context_free(&io);
    }
}

static bool is_mp4(const AVFormatContext* format)
{
    return std::strstr(format->iformat->name, "mp4") != nullptr;
}

// AAC by name: a libfdk_aac in the same build reads HE-AAC at half the length
static const AVCodec* decoder_of(const AVStream* stream)
{
    return stream->codecpar->codec_id == AV_CODEC_ID_AAC ? avcodec_find_decoder_by_name("aac") : avcodec_find_decoder(stream->codecpar->codec_id);
}

// Any audio stream: av_find_best_stream() passes over one FFmpeg knows little of
static const AVStream* first_audio_stream(const AVFormatContext* format)
{
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            return format->streams[i];
        }
    }
    return nullptr;
}

// One channel of samples to floats: Bits is the full scale, 0 for samples that are floats
template<typename Sample, int Bits>
static void copy_samples(const uint8_t* data, int step, int64_t first, nframes_t count, audio_sample_t* out)
{
    constexpr float FullScale = float(1ull << Bits);
    const Sample* samples = reinterpret_cast<const Sample*>(data) + first;
    for (nframes_t i = 0; i < count; ++i) {
        out[i] = float(samples[int64_t(i) * step]) / FullScale;
    }
}

// The formats read, and how: nullptr for any other
static FFmpegAudioReader::SampleCopy sample_copy_for(int format)
{
    switch (av_get_packed_sample_fmt(AVSampleFormat(format))) {
    case AV_SAMPLE_FMT_FLT:
        return copy_samples<float, 0>;
    case AV_SAMPLE_FMT_DBL:
        return copy_samples<double, 0>;
    case AV_SAMPLE_FMT_S32:
        return copy_samples<int32_t, 31>;
    case AV_SAMPLE_FMT_S16:
        return copy_samples<int16_t, 15>;
    default:
        return nullptr;
    }
}

// The format of stream in words, for the user who has no decoder for it
static QString format_in_words(const AVFormatContext* format, const AVStream* stream)
{
    const AVCodecParameters* codec = stream->codecpar;
    QString name = QString::fromUtf8(avcodec_get_name(codec->codec_id)).toUpper();
    if (const char* profile = avcodec_profile_name(codec->codec_id, codec->profile)) {
        const QString words = QString::fromUtf8(profile);
        name = words.contains(name, Qt::CaseInsensitive) ? words : QStringLiteral("%1 %2").arg(name, words);
    }
    return is_mp4(format) ? QStringLiteral("M4A with %1").arg(name) : name;
}

// Where the audio starts behind an ID3v2 tag, which FFmpeg's demuxer skips
static int64_t behind_id3(TByteSource& source)
{
    constexpr int TagHeaderBytes = 10;
    constexpr int FooterFlag = 0x10;
    unsigned char tag[TagHeaderBytes];
    source.seek(0);
    if (source.read(tag, TagHeaderBytes) != TagHeaderBytes || std::memcmp(tag, "ID3", 3) != 0 || (tag[6] | tag[7] | tag[8] | tag[9]) & 0x80) {
        return 0;
    }
    // The tag says its size: nothing to search, so no limit
    const int64_t footer = (tag[5] & FooterFlag) ? TagHeaderBytes : 0;
    return TagHeaderBytes + footer + ((tag[6] << 21) | (tag[7] << 14) | (tag[8] << 7) | tag[9]);
}

// ADTS has no signature but its frames: a sync word, layer 0, a sample rate of
// the table and a length that leads to the next frame or the end of the file
static bool holds_adts(TByteSource& source)
{
    constexpr int HeaderBytes = 7;
    constexpr int FramesToSee = 2;
    constexpr int SampleRates = 13;
    int64_t at = behind_id3(source);
    const int64_t size = source.size();
    for (int frame = 0; frame < FramesToSee; ++frame) {
        unsigned char header[HeaderBytes];
        source.seek(at);
        if (source.read(header, HeaderBytes) != HeaderBytes) {
            return false;
        }
        const bool sync = header[0] == 0xff && (header[1] & 0xf6) == 0xf0;
        const int rate = (header[2] >> 2) & 0x0f;
        const int bytes = ((header[3] & 0x03) << 11) | (header[4] << 3) | (header[5] >> 5);
        if (!sync || rate >= SampleRates || bytes < HeaderBytes || at + bytes > size) {
            return false;
        }
        at += bytes;
        if (at == size) {
            return true;
        }
    }
    return true;
}

// An MP4's boxes come first; only FFmpeg's own reading says it has audio
static bool mp4_with_audio(TByteSource& source)
{
    AVIOContext* io = nullptr;
    AVFormatContext* format = nullptr;
    const bool opened = open_format(source, io, format) >= 0;
    const bool audio = opened && is_mp4(format) && first_audio_stream(format);
    close_format(io, format);
    return audio;
}

bool FFmpegAudioReader::can_decode(TByteSource& source)
{
    char head[8]{};
    source.seek(0);
    source.read(head, sizeof(head));
    for (const char* box : {"ftyp", "moov", "mdat", "free", "wide"}) {
        if (std::memcmp(head + 4, box, 4) == 0) {
            return mp4_with_audio(source);
        }
    }
    return holds_adts(source);
}

FFmpegAudioReader::FFmpegAudioReader(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
    : TAudioDecoder(fileName, std::move(source), opener)
{
}

FFmpegAudioReader::~FFmpegAudioReader()
{
    av_frame_free(&m_frameBuffer);
    av_packet_free(&m_packet);
    avcodec_free_context(&m_codec);
    close_format(m_io, m_format);
}

TAudioFileStatus FFmpegAudioReader::start()
{
    m_packet = av_packet_alloc();
    m_frameBuffer = av_frame_alloc();
    if (!m_packet || !m_frameBuffer) {
        return open_error(QStringLiteral("out of memory"));
    }
    if (int error = open_format(source(), m_io, m_format); error < 0) {
        return open_error(QStringLiteral("not a readable MP4 or AAC file: %1").arg(words_of(error)));
    }
    if (int error = avformat_find_stream_info(m_format, nullptr); error < 0) {
        return open_error(words_of(error));
    }
    m_stream = av_find_best_stream(m_format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_stream < 0) {
        const AVStream* unread = first_audio_stream(m_format);
        if (!unread) {
            return open_error(QStringLiteral("no audio in it"));
        }
        return open_error(decoder_of(unread) ? QStringLiteral("%1: the file does not say enough of its audio").arg(format_in_words(m_format, unread))
                                             : QStringLiteral("%1: this FFmpeg has no decoder for it").arg(format_in_words(m_format, unread)));
    }
    // A video would be read with the audio, and dropped
    for (unsigned i = 0; i < m_format->nb_streams; ++i) {
        m_format->streams[i]->discard = int(i) == m_stream ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }
    const AVStream* stream = m_format->streams[m_stream];
    m_decoder = decoder_of(stream);
    if (!m_decoder) {
        return open_error(QStringLiteral("%1: this FFmpeg has no decoder for it").arg(format_in_words(m_format, stream)));
    }
    if (TAudioFileStatus opened = open_codec(); !opened) {
        return opened;
    }
    m_channels = uint(m_codec->ch_layout.nb_channels);
    m_rate = uint(m_codec->sample_rate);

    TAudioFileStatus counted = is_mp4(m_format) ? mp4_length() : count_frames();
    if (!counted) {
        return counted;
    }
    if (m_sampleFormat >= 0 && !m_sampleCopy) {
        return open_error(QStringLiteral("samples as %1 are not read").arg(QString::fromUtf8(av_get_sample_fmt_name(AVSampleFormat(m_sampleFormat)))));
    }
    if (TAudioFileStatus sought = seek_frame(0); !sought) {
        return sought;
    }
    return require_audio();
}

TAudioFileStatus FFmpegAudioReader::open_codec()
{
    AVCodecContext* codec = avcodec_alloc_context3(m_decoder);
    if (!codec) {
        return open_error(QStringLiteral("out of memory"));
    }
    if (int error = avcodec_parameters_to_context(codec, m_format->streams[m_stream]->codecpar); error < 0) {
        avcodec_free_context(&codec);
        return open_error(words_of(error));
    }
    // The decoder leaves out what the file asks to skip, and moves the stamps with it
    codec->pkt_timebase = m_format->streams[m_stream]->time_base;
    // One decoder, one thread: the readers run where the disk IO runs
    codec->thread_count = 1;
    if (int error = avcodec_open2(codec, m_decoder, nullptr); error < 0) {
        avcodec_free_context(&codec);
        return open_error(words_of(error));
    }
    avcodec_free_context(&m_codec);
    m_codec = codec;
    return {};
}

// What the first packets of the file say: their stamps, and the first frame the
// decoder gives, after what it left out
void FFmpegAudioReader::note_packet()
{
    if (m_packetsSeen == 0) {
        m_startTimestamp = m_packet->pts != AV_NOPTS_VALUE ? m_packet->pts : 0;
        m_startDts = m_packet->dts != AV_NOPTS_VALUE ? m_packet->dts : m_startTimestamp;
        m_firstDuration = m_packet->duration;
        size_t size = 0;
        if (const uint8_t* side = av_packet_get_side_data(m_packet, AV_PKT_DATA_SKIP_SAMPLES, &size); side && size >= 4) {
            std::memcpy(&m_packetSkip, side, 4);
        }
    } else if (m_packetsSeen == 1) {
        m_secondTimestamp = m_packet->pts;
    }
    ++m_packetsSeen;
    if (m_firstFrameSamples > 0) {
        return;
    }
    avcodec_send_packet(m_codec, m_packet);
    if (avcodec_receive_frame(m_codec, m_frameBuffer) == 0) {
        m_firstFrameSamples = m_frameBuffer->nb_samples;
        m_firstFrameStamp = frame_stamp();
        note_format();
    }
}

// What the decoder gives counts, not what the headers say of it
void FFmpegAudioReader::note_format()
{
    m_sampleFormat = m_frameBuffer->format;
    m_sampleCopy = sample_copy_for(m_sampleFormat);
    m_channels = uint(m_frameBuffer->ch_layout.nb_channels);
    m_rate = uint(m_frameBuffer->sample_rate);
}

// The frames the decoder has now: where they end, past end. None of the first packets
// decoded: the file's format comes from here
int64_t FFmpegAudioReader::take_frames(int64_t end)
{
    while (avcodec_receive_frame(m_codec, m_frameBuffer) == 0) {
        end = position_of_frame() + m_frameBuffer->nb_samples;
        if (m_sampleFormat < 0) {
            note_format();
        }
    }
    return end;
}

// An MP4 knows its length: the header and the end of the tables, both from frame 0.
// A packet apart is healthy, the tables further below the header is damage, which
// ends in an error, and further above it is refused. The file's last packet gives
// the tables' end and what the decoder has there
TAudioFileStatus FFmpegAudioReader::mp4_length()
{
    AVStream* stream = m_format->streams[m_stream];
    const AVRational rate{1, int(m_rate)};
    while (m_packetsSeen < 2 || (m_firstFrameSamples == 0 && m_packetsSeen < FirstFramePackets)) {
        const int error = av_read_frame(m_format, m_packet);
        if (error == AVERROR_EOF) {
            break;
        }
        if (error < 0) {
            return open_error(words_of(error));
        }
        if (m_packet->stream_index == m_stream) {
            note_packet();
        }
        av_packet_unref(m_packet);
    }
    if (m_packetsSeen == 0) {
        return open_error(QStringLiteral("no audio in it"));
    }
    avcodec_flush_buffers(m_codec);
    // Frame 0 is where the file's skip ends, or else the decoder's first frame: a first
    // packet that does not decode must not move the audio
    if (m_packetSkip > 0) {
        m_firstFrameStamp = m_startTimestamp + av_rescale_q(m_packetSkip, rate, stream->time_base);
    }
    m_firstTimestamp = m_firstFrameSamples > 0 || m_packetSkip > 0 ? m_firstFrameStamp : m_startTimestamp;
    // Tables can give the first packet more than the decoder has: the audio runs on
    // and every later stamp lies that much behind
    const int64_t skipped = av_rescale_q(m_firstTimestamp - m_startTimestamp, stream->time_base, rate);
    if (const int64_t beyond = m_firstDuration - av_rescale_q(m_firstFrameSamples + skipped, rate, stream->time_base); m_firstFrameSamples > 0 && beyond > 0) {
        m_firstTimestamp += beyond;
    }
    if (stream->duration == AV_NOPTS_VALUE) {
        return open_error(QStringLiteral("no length in the header"));
    }

    int64_t target = stream->duration;
    int64_t lastTimestamp = m_startTimestamp;
    int64_t lastDuration = m_firstDuration;
    if (const int entries = avformat_index_get_entries_count(stream); entries > 0) {
        lastTimestamp = avformat_index_get_entry(stream, entries - 1)->timestamp;
        target = std::max(target, lastTimestamp);
    }
    // From a little earlier: a decoder can need the packets before the last
    target = std::max(target - av_rescale_q(PreRollFrames, rate, stream->time_base), m_startDts);
    if (int error = av_seek_frame(m_format, m_stream, target, AVSEEK_FLAG_BACKWARD); error < 0) {
        return open_error(words_of(error));
    }
    avcodec_flush_buffers(m_codec);
    int64_t decodedEnd = 0;
    while (av_read_frame(m_format, m_packet) >= 0) {
        if (m_packet->stream_index == m_stream) {
            if (m_packet->pts != AV_NOPTS_VALUE) {
                lastTimestamp = m_packet->pts;
                lastDuration = m_packet->duration;
            }
            avcodec_send_packet(m_codec, m_packet);
            decodedEnd = take_frames(decodedEnd);
        }
        av_packet_unref(m_packet);
    }
    avcodec_send_packet(m_codec, nullptr);
    decodedEnd = take_frames(decodedEnd);

    const int64_t packet = av_rescale_q(m_firstDuration, stream->time_base, rate);
    // The header's duration is a span: it starts at the track's first stamp, also
    // behind an empty edit. A first stamp that comes twice is the second packet's
    const int64_t headerStart = m_secondTimestamp == m_startTimestamp ? m_startTimestamp - m_firstDuration
                                : stream->start_time != AV_NOPTS_VALUE ? stream->start_time : m_startTimestamp;
    const int64_t headerEnd = av_sat_add64(headerStart, stream->duration);
    const int64_t header = av_rescale_q(av_sat_sub64(headerEnd, m_firstTimestamp), stream->time_base, rate);
    const int64_t tables = av_rescale_q(av_sat_sub64(av_sat_add64(lastTimestamp, lastDuration), m_firstTimestamp), stream->time_base, rate);
    const int64_t apart = av_sat_sub64(header, tables);
    if (av_sat_sub64(tables, header) > packet) {
        return open_error(QStringLiteral("the tables hold more than the header's %1 frames").arg(header));
    }
    int64_t frames = apart > packet ? header : std::min(header, tables);
    if (decodedEnd > 0 && frames > decodedEnd && frames - decodedEnd <= packet) {
        frames = decodedEnd;
    }
    return take_length(frames);
}

// A file without a length: every packet once, without what it asks to skip
TAudioFileStatus FFmpegAudioReader::count_frames()
{
    const AVStream* stream = m_format->streams[m_stream];
    const AVRational rate{1, int(m_rate)};
    int64_t duration = 0;
    while (true) {
        const int error = av_read_frame(m_format, m_packet);
        if (error == AVERROR_EOF) {
            break;
        }
        if (error < 0) {
            return open_error(words_of(error));
        }
        if (m_packet->stream_index == m_stream) {
            note_packet();
            duration += m_packet->duration;
        }
        av_packet_unref(m_packet);
    }
    if (m_packetsSeen == 0) {
        return open_error(QStringLiteral("no audio in it"));
    }
    avcodec_flush_buffers(m_codec);
    m_firstTimestamp = m_firstFrameStamp;
    if (m_firstTimestamp == m_startTimestamp && m_codec->codec_id == AV_CODEC_ID_AAC && m_secondTimestamp != AV_NOPTS_VALUE) {
        // ADTS has no word on priming and the decoder none either: one AAC frame,
        // as FAAD leaves out
        m_firstTimestamp = m_secondTimestamp;
    }
    m_firstFrameStamp = m_firstTimestamp;
    return take_length(av_rescale_q(duration - (m_firstTimestamp - m_startTimestamp), stream->time_base, rate));
}

TAudioFileStatus FFmpegAudioReader::take_length(int64_t frames)
{
    if (frames <= 0 || frames > std::numeric_limits<nframes_t>::max()) {
        return open_error(QStringLiteral("%1 frames of audio").arg(frames));
    }
    m_frames = nframes_t(frames);
    return {};
}

int64_t FFmpegAudioReader::frame_stamp() const
{
    return m_frameBuffer->pts != AV_NOPTS_VALUE ? m_frameBuffer->pts : m_frameBuffer->best_effort_timestamp;
}

int64_t FFmpegAudioReader::position_of_frame() const
{
    const AVStream* stream = m_format->streams[m_stream];
    const int64_t timestamp = frame_stamp();
    // No stamp: it follows the frame before
    if (timestamp == AV_NOPTS_VALUE) {
        return m_decodedFirst + m_decodedCount;
    }
    // The first frame is frame 0, whatever comes after it
    if (timestamp == m_firstFrameStamp) {
        return 0;
    }
    return av_rescale_q(av_sat_sub64(timestamp, m_firstTimestamp), stream->time_base, AVRational{1, int(m_rate)});
}

bool FFmpegAudioReader::decode_next()
{
    while (true) {
        const int received = avcodec_receive_frame(m_codec, m_frameBuffer);
        if (received == 0) {
            // A file of one format: copy_decoded() reads the open's channels
            if (uint(m_frameBuffer->ch_layout.nb_channels) != m_channels || m_frameBuffer->format != m_sampleFormat || uint(m_frameBuffer->sample_rate) != m_rate) {
                m_lastError = QStringLiteral("the audio's format changes inside the file");
                return false;
            }
            m_decodedFirst = position_of_frame();
            m_decodedCount = m_frameBuffer->nb_samples;
            return true;
        }
        if (received == AVERROR_EOF) {
            return false;
        }
        // Any other error: a packet that does not decode stays silence, the next
        // one goes on
        if (m_decoderDrained) {
            return false;
        }
        const int read = av_read_frame(m_format, m_packet);
        if (read == AVERROR_EOF) {
            m_decoderDrained = true;
            avcodec_send_packet(m_codec, nullptr);
            continue;
        }
        if (read < 0) {
            m_lastError = words_of(read);
            return false;
        }
        if (m_packet->stream_index == m_stream) {
            avcodec_send_packet(m_codec, m_packet);
        }
        av_packet_unref(m_packet);
    }
}

TAudioFileStatus FFmpegAudioReader::seek_frame(nframes_t frame)
{
    Q_ASSERT(frame < m_frames);
    const AVStream* stream = m_format->streams[m_stream];
    const AVRational rate{1, int(m_rate)};
    const bool fromStart = int64_t(frame) <= PreRollFrames;
    const int64_t target = fromStart ? m_startDts : m_firstTimestamp + av_rescale_q(int64_t(frame) - PreRollFrames, rate, stream->time_base);
    if (int error = av_seek_frame(m_format, m_stream, target, AVSEEK_FLAG_BACKWARD); error < 0) {
        return seek_failed(frame, words_of(error));
    }
    // A flush leaves the window of the last block: the file's first frames would
    // differ from a read from the start
    if (fromStart) {
        if (TAudioFileStatus opened = open_codec(); !opened) {
            return seek_failed(frame, opened.error().detail);
        }
    } else {
        avcodec_flush_buffers(m_codec);
    }
    m_decoderDrained = false;
    m_decodedCount = 0;
    m_lastError.clear();
    while (m_decodedCount == 0 || m_decodedFirst + m_decodedCount <= int64_t(frame)) {
        if (!decode_next()) {
            return seek_failed(frame, m_lastError);
        }
    }
    return seek_landed(frame);
}

void FFmpegAudioReader::copy_decoded(TAudioBuffer& buffer, nframes_t at, int64_t first, nframes_t count)
{
    Q_ASSERT(m_sampleCopy);
    const bool planar = av_sample_fmt_is_planar(AVSampleFormat(m_frameBuffer->format));
    const int channels = int(m_channels);
    for (int channel = 0; channel < channels; ++channel) {
        audio_sample_t* out = buffer.get_channel_buffer(uint(channel)).get_data(at + count) + at;
        const uint8_t* data = m_frameBuffer->extended_data[planar ? channel : 0];
        const int step = planar ? 1 : channels;
        const int offset = planar ? 0 : channel;
        m_sampleCopy(data, step, first * step + offset, count, out);
    }
}

TAudioFileResult FFmpegAudioReader::read_frames(TAudioBuffer& buffer, nframes_t count)
{
    if (count == 0) {
        return {};
    }
    buffer.check_capacity(count, m_channels);
    const nframes_t wanted = std::min(count, m_frames - m_nextFrame);
    if (m_inErrorState || wanted == 0) {
        return read_done(buffer, 0, count);
    }

    nframes_t filled = 0;
    while (filled < wanted) {
        const int64_t position = int64_t(m_nextFrame) + filled;
        if (position >= m_decodedFirst + m_decodedCount) {
            if (!decode_next()) {
                break;
            }
            continue;
        }
        if (position < m_decodedFirst) {
            // Packets that did not decode
            const nframes_t gap = nframes_t(std::min<int64_t>(m_decodedFirst - position, wanted - filled));
            buffer.silence(filled, gap, m_channels);
            filled += gap;
            continue;
        }
        const nframes_t take = nframes_t(std::min<int64_t>(m_decodedFirst + m_decodedCount - position, wanted - filled));
        copy_decoded(buffer, filled, position - m_decodedFirst, take);
        filled += take;
    }
    return read_done(buffer, filled, count, m_lastError);
}
