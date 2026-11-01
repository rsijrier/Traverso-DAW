// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TAudioDecoder.h"

struct AVCodec;
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVIOContext;
struct AVPacket;

/**
 * @brief AAC in ADTS and whatever an MP4 holds, through libavformat and libavcodec.
 *
 * All bytes go through TByteSource, by an AVIOContext. The open of an MP4 reads the
 * first packets and the last, of ADTS every packet once for the length; a seek goes
 * back by PreRollFrames and decodes up to the frame, by the frames' time stamps.
 */
class FFmpegAudioReader : public TAudioDecoder
{
public:
    ~FFmpegAudioReader() override;

    // Looks for ADTS frames or an MP4 with audio in it
    static bool can_decode(TByteSource& source);

    QString decoder_type() const override { return "ffmpeg"; }

    TAudioFileStatus seek_frame(nframes_t frame) override;
    TAudioFileResult read_frames(TAudioBuffer& buffer, nframes_t count) override;

private:
    friend struct TAudioDecoderFactory;

    FFmpegAudioReader(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener);
    TAudioFileStatus start();
    TAudioFileStatus open_codec();
    void note_packet();
    void note_format();
    void place_frame_zero(bool mp4);
    TAudioFileStatus mp4_length();
    TAudioFileStatus count_frames();
    TAudioFileStatus take_length(int64_t frames);
    // The next decoded frame in m_frameBuffer; false at the end of the file or
    // on an error, which goes to m_lastError
    bool decode_next();
    // The file's frame the time stamp of m_frameBuffer stands for
    int64_t position_of_frame() const;
    void copy_decoded(TAudioBuffer& buffer, nframes_t at, int64_t first, nframes_t count);

    AVFormatContext* m_format{nullptr};
    AVIOContext* m_io{nullptr};
    AVCodecContext* m_codec{nullptr};
    const AVCodec* m_decoder{nullptr};
    AVPacket* m_packet{nullptr};
    AVFrame* m_frameBuffer{nullptr};
    int m_stream{-1};
    // The time stamps of frame 0 and of the first packet, which starts m_priming
    // frames before frame 0
    int64_t m_firstTimestamp{0};
    int64_t m_startTimestamp{0};
    int64_t m_startDts{0};
    int64_t m_priming{0};
    int m_packetsSeen{0};
    int64_t m_firstDuration{0};
    int64_t m_secondTimestamp{INT64_MIN}; // AV_NOPTS_VALUE
    bool m_skipKnown{false};
    int m_firstFrameSamples{0};
    // Of the first frame, an AVSampleFormat
    int m_sampleFormat{-1};
    // The next frame decoded is the file's first
    bool m_fromStart{false};
    // The file's frames in m_frameBuffer: [m_decodedFirst, m_decodedFirst + m_decodedCount)
    int64_t m_decodedFirst{0};
    int64_t m_decodedCount{0};
    bool m_decoderDrained{false};
    QString m_lastError;
};
