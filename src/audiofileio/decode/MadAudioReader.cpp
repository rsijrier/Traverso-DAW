/*
Copyright (C) 2007-2026 Ben Levitt
 * This file is based on the mp3 decoding plugin of the K3b project.
 * Copyright (C) 1998-2007 Sebastian Trueg <trueg@k3b.org>

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

#include "MadAudioReader.h"
#include <QString>
#include <QVector>

#include <algorithm>
#include <cstring>

#include "TAudioBuffer.h"



static const int INPUT_BUFFER_SIZE = 5*8192;


K3bMad::K3bMad()
  : m_madStructuresInitialized(false),
    m_bInputError(false),
    m_eofPadded(false)
{
    madStream = new mad_stream;
    madFrame  = new mad_frame;
    madSynth  = new mad_synth;
    madTimer  = new mad_timer_t;

    m_inputBuffer = nullptr;
}


K3bMad::~K3bMad()
{
    cleanup();

    delete madStream;
    delete madFrame;
    delete madSynth;
    delete madTimer;

    clearInputBuffer();
}


void K3bMad::createInputBuffer()
{
    if (!m_inputBuffer) {
        //
        // we allocate additional MAD_BUFFER_GUARD bytes to always be able to append the
        // zero bytes needed for decoding the last frame.
        //
        m_inputBuffer = new unsigned char[INPUT_BUFFER_SIZE+MAD_BUFFER_GUARD];
        memset(m_inputBuffer, 0, INPUT_BUFFER_SIZE+MAD_BUFFER_GUARD);
    }
}


void K3bMad::clearInputBuffer()
{
    if (m_inputBuffer) {
        delete [] m_inputBuffer;
        m_inputBuffer = nullptr;
    }
}


bool K3bMad::open(TAudioByteSource* input)
{
    cleanup();

    m_bInputError = false;
    m_eofPadded = false;
    m_channels = m_sampleRate = 0;

    m_input = input;

    if (!m_input || !m_input->seek(0)) {
        return false;
    }

    initMad();

    return true;
}


bool K3bMad::inputError() const
{
    return m_bInputError;
}


bool K3bMad::fillStreamBuffer()
{
    if (!m_input) {
        m_bInputError = true;
        return false;
    }

    /* The input bucket must be filled if it becomes empty or if
    * it's the first execution of the loop.
    */
    if (madStream->buffer == nullptr || madStream->error == MAD_ERROR_BUFLEN) {
        if (!m_inputBuffer) {
            createInputBuffer();
        }

        long remaining = 0;
        unsigned char* readStart;

        if (madStream->next_frame != nullptr) {
            remaining = madStream->bufend - madStream->next_frame;
            if (remaining > 0) {
                memmove(m_inputBuffer, madStream->next_frame, remaining);
            }
            readStart = m_inputBuffer + remaining;
        }
        else {
            readStart = m_inputBuffer;
            remaining = 0;
        }

        long readSize = INPUT_BUFFER_SIZE - remaining;
        long result = 0;

        if (!m_input->at_end()) {
            result = m_input->read((char*)readStart, readSize);
            if (result < 0) {
                m_bInputError = true;
                return false;
            }
        }

        // The zeros libmad needs behind the last frame are no part of the
        // file: streamPos() leaves them out
        m_guardBytes = 0;
        if (result == 0) {
            if (m_eofPadded) {
                return false;
            }
            memset(readStart, 0, MAD_BUFFER_GUARD);
            result = MAD_BUFFER_GUARD;
            m_guardBytes = MAD_BUFFER_GUARD;
            m_eofPadded = true;
        }
        else if (m_input->at_end()) {
            memset(readStart + result, 0, MAD_BUFFER_GUARD);
            result += MAD_BUFFER_GUARD;
            m_guardBytes = MAD_BUFFER_GUARD;
            m_eofPadded = true;
        }

        // Pipe the new buffer content to libmad's stream decoder facility.
        mad_stream_buffer(madStream, m_inputBuffer, result + remaining);
        madStream->error = MAD_ERROR_NONE;
    }

    return true;
}


bool K3bMad::skipTag()
{
    // skip the tag at the beginning of the file
    m_input->seek(0);

    //
    // now check if the file starts with an id3 tag and skip it if so
    //
    // The ten bytes of an ID3v2 header are all it takes (a file shorter than
    // that has no tag; a short MP3 is a file, too)
    char buf[10];
    if (m_input->read(buf, 10) < 10) {
        return m_input->seek(0);
    }

    if ((buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') &&
       ((unsigned short)buf[3] < 0xff && (unsigned short)buf[4] < 0xff)) {
        // do we have a footer?
        bool footer = (buf[5] & 0x10);

        // the size is saved as a synched int meaning bit 7 is always cleared to 0
        unsigned int size =
            ( (buf[6] & 0x7f) << 21 ) |
            ( (buf[7] & 0x7f) << 14 ) |
            ( (buf[8] & 0x7f) << 7) |
            (buf[9] & 0x7f);
        unsigned int offset = size + 10;

        if (footer) {
            offset += 10;
        }

        // skip the id3 tag
        if (!m_input->seek(offset)) {
            return false;
        }
    }
    else {
        // reset file
        return m_input->seek(0);
    }

    return true;
}


bool K3bMad::isXingOrInfoFrame(const mad_header* header, const unsigned char* frame_data, size_t frame_len)
{
    if (!header || !frame_data) {
        return false;
    }

    size_t side_info_len = 0;
    if (header->flags & MAD_FLAG_MPEG_2_5_EXT) {
        // MPEG 2.5
        side_info_len = (header->mode == MAD_MODE_SINGLE_CHANNEL) ? 9 : 17;
    } else if (header->layer == MAD_LAYER_III) {
        if (header->flags & MAD_FLAG_LSF_EXT) {
            // MPEG 2
            side_info_len = (header->mode == MAD_MODE_SINGLE_CHANNEL) ? 9 : 17;
        } else {
            // MPEG 1
            side_info_len = (header->mode == MAD_MODE_SINGLE_CHANNEL) ? 17 : 32;
        }
    } else {
        return false;
    }

    size_t xing_offset = 4 + side_info_len;
    if (frame_len >= xing_offset + 4) {
        const unsigned char* p = frame_data + xing_offset;
        if ((p[0] == 'X' && p[1] == 'i' && p[2] == 'n' && p[3] == 'g') ||
            (p[0] == 'I' && p[1] == 'n' && p[2] == 'f' && p[3] == 'o')) {
            return true;
        }
    }

    // Check VBRI header at offset 36 (4 + 32)
    if (frame_len >= 36 + 4) {
        const unsigned char* p = frame_data + 36;
        if (p[0] == 'V' && p[1] == 'B' && p[2] == 'R' && p[3] == 'I') {
            return true;
        }
    }

    return false;
}


bool K3bMad::read_lame_tag(const mad_header* header, const unsigned char* frame_data, size_t frame_len, int& delay, int& padding)
{
    // The Xing/Info header sits behind the side information, a CRC first
    // when the frame has one
    size_t offset = 4;
    if (header->flags & MAD_FLAG_PROTECTION) {
        offset += 2;
    }
    if (header->flags & (MAD_FLAG_MPEG_2_5_EXT | MAD_FLAG_LSF_EXT)) {
        offset += (header->mode == MAD_MODE_SINGLE_CHANNEL) ? 9 : 17;
    } else {
        offset += (header->mode == MAD_MODE_SINGLE_CHANNEL) ? 17 : 32;
    }
    if (frame_len < offset + 8) {
        return false;
    }
    const unsigned char* p = frame_data + offset;
    if (memcmp(p, "Xing", 4) != 0 && memcmp(p, "Info", 4) != 0) {
        return false;
    }
    // The fields the flags say are there: frames, bytes, table of contents,
    // quality
    const unsigned flags = (unsigned(p[4]) << 24) | (unsigned(p[5]) << 16) | (unsigned(p[6]) << 8) | unsigned(p[7]);
    size_t lame = offset + 8;
    lame += (flags & 1) ? 4 : 0;
    lame += (flags & 2) ? 4 : 0;
    lame += (flags & 4) ? 100 : 0;
    lame += (flags & 8) ? 4 : 0;
    // The LAME extension (also ffmpeg's): the encoder's name, and 21 bytes
    // in the encoder delay and the padding, 12 bits each
    if (frame_len < lame + 24) {
        return false;
    }
    const unsigned char* q = frame_data + lame;
    if (memcmp(q, "LAME", 4) != 0 && memcmp(q, "Lavc", 4) != 0 && memcmp(q, "Lavf", 4) != 0) {
        return false;
    }
    delay = (int(q[21]) << 4) | (int(q[22]) >> 4);
    padding = ((int(q[22]) & 0x0f) << 8) | int(q[23]);
    return true;
}


bool K3bMad::seekFirstHeader()
{
    //
    // A lot of mp3 files start with a lot of junk which confuses mad.
    // We "allow" an mp3 file to start with at most 1 KB of junk. This is just
    // some random value since we do not want to search the hole file. That would
    // take way to long for non-mp3 files.
    //
    bool headerFound = findNextHeader();
    qint64 startInputPos = streamPos();
    while (!headerFound &&
       !m_input->at_end() &&
       streamPos() <= startInputPos+1024) {
        headerFound = findNextHeader();
    }

    if (headerFound) {
        size_t frameLen = madStream->next_frame ? (madStream->next_frame - madStream->this_frame) : 0;
        if (isXingOrInfoFrame(&madFrame->header, madStream->this_frame, frameLen)) {
            // The encoder's delay and padding, when it wrote them (LAME,
            // ffmpeg): the gapless information
            read_lame_tag(&madFrame->header, madStream->this_frame, frameLen, encoderDelay, encoderPadding);
            // Skip the Xing/Info/VBRI metadata frame so it isn't decoded as silence
            headerFound = findNextHeader();
        }
    }

    // seek back to the begin of the audio frame
    if (headerFound) {
        qint64 targetPos = streamPos();
        m_input->seek(targetPos);
    }

    // reset the stream to make sure mad really starts decoding at our seek position
    mad_stream_finish(madStream);
    mad_stream_init(madStream);
    m_eofPadded = false;

    return headerFound;
}


bool K3bMad::eof() const
{
    return m_input->at_end();
}


qint64 K3bMad::inputPos() const
{
    return m_input->position();
}


qint64 K3bMad::streamPos() const
{
    return inputPos() - (madStream->bufend - madStream->this_frame - m_guardBytes);
}


bool K3bMad::inputSeek(qint64 pos)
{
    mad_stream_finish(madStream);
    mad_stream_init(madStream);
    m_eofPadded = false;
    m_guardBytes = 0;
    return m_input->seek(pos);
}


void K3bMad::initMad()
{
    if (!m_madStructuresInitialized) {
        mad_stream_init(madStream);
        mad_timer_set(madTimer, 0, 0, 0);
        mad_frame_init(madFrame);
        mad_synth_init(madSynth);

        m_madStructuresInitialized = true;
    }
}


void K3bMad::cleanup()
{
    m_eofPadded = false;
    m_input = nullptr;

    if (m_madStructuresInitialized) {
        mad_frame_finish(madFrame);
        mad_synth_finish(madSynth);
        mad_stream_finish(madStream);
    }

    m_madStructuresInitialized = false;
}


//
// LOSTSYNC could happen when mad encounters the id3 tag...
//
bool K3bMad::findNextHeader()
{
    if (!fillStreamBuffer()) {
        return false;
    }

    //
    // MAD_RECOVERABLE == true:  frame was read, decoding failed (about to skip frame)
    // MAD_RECOVERABLE == false: frame was not read, need data
    //

    if (mad_header_decode( &madFrame->header, madStream ) < 0) {
        if (MAD_RECOVERABLE(madStream->error) ||
           madStream->error == MAD_ERROR_BUFLEN) {
            return findNextHeader();
        }
        else
        //      kdDebug() << "(K3bMad::findNextHeader) error: " << mad_stream_errorstr( madStream ) << endl;

        // FIXME probably we should not do this here since we don't do it
        // in the frame decoding
        //     if(!checkFrameHeader(&madFrame->header))
        //       return findNextHeader();

        return false;
    }

    if (!m_channels) {
        m_channels = MAD_NCHANNELS(&madFrame->header);
        m_sampleRate = madFrame->header.samplerate;
    }

    mad_timer_add(madTimer, madFrame->header.duration);

    return true;
}


bool K3bMad::decodeNextFrame()
{
    if (!fillStreamBuffer()) {
        return false;
    }

    if (mad_frame_decode(madFrame, madStream) < 0) {
        if (MAD_RECOVERABLE(madStream->error) ||
            madStream->error == MAD_ERROR_BUFLEN) {
            return decodeNextFrame();
        }

        return false;
    }

    if (!m_channels) {
        m_channels = MAD_NCHANNELS(&madFrame->header);
        m_sampleRate = madFrame->header.samplerate;
    }

    mad_timer_add(madTimer, madFrame->header.duration);

    return true;
}


//
// This is from the arts mad decoder
//
bool K3bMad::checkFrameHeader(mad_header* header) const
{
    int frameSize = MAD_NSBSAMPLES(header) * 32;

    if (frameSize <= 0) {
        return false;
    }

    if (m_channels && m_channels != MAD_NCHANNELS(header)) {
        return false;
    }

    return true;
}



class MadAudioReader::MadDecoderPrivate
{
public:
    MadDecoderPrivate()
    {
        mad_header_init(&firstHeader);
    }

    K3bMad* handle{};
    // Where every audio frame starts in the file (the Xing frame not)
    QVector<unsigned long long> seekPositions;

    mad_header firstHeader{};
    bool vbr{};
    nframes_t samplesPerFrame{1152};
    // The decoder's samples before output frame 0: with gapless information
    // the encoder's delay and the decoder's own (529 samples)
    nframes_t skip{0};

    // The audio frame decode_next_frame() decodes next
    qint64 nextFrame{0};
    // The samples of the frame decoded last, per channel; pcmStart: the
    // first not read yet
    float pcm[2][1152]{};
    nframes_t pcmStart{0};
    nframes_t pcmCount{0};
};



MadAudioReader::MadAudioReader(const QString& filename, const TAudioSourceOpener& opener)
 : TAudioDecoder(filename, opener)
{
    d = new MadDecoderPrivate();
    d->handle = new K3bMad();
    m_source = open_source(m_fileName);

    if (!initDecoderInternal()) {
        m_openError = TAudioFileError{TAudioFileError::Code::CannotOpen, QStringLiteral("MAD finds no MPEG audio header in it")};
    }

    const unsigned long audioFrames = countFrames();

    switch( d->firstHeader.mode ) {
        case MAD_MODE_SINGLE_CHANNEL:
            m_channels = 1;
            break;
        case MAD_MODE_DUAL_CHANNEL:
        case MAD_MODE_JOINT_STEREO:
        case MAD_MODE_STEREO:
            m_channels = 2;
    }

    if (audioFrames == 0) {
        if (!m_openError) {
            m_openError = TAudioFileError{TAudioFileError::Code::CannotOpen,
                                          d->handle->inputError() ? QStringLiteral("reading it failed") : QStringLiteral("MAD finds no MPEG audio frames in it")};
        }
        d->handle->cleanup();
        delete d->handle;
        delete d;
        d = nullptr;
        return;
    }

    m_rate = d->firstHeader.samplerate;

    // Gapless: the audio starts behind the encoder's delay and the
    // decoder's (529 samples of the synthesis filter, as mpg123 and ffmpeg
    // count it), and ends before the encoder's padding. Without the
    // information all frames are audio.
    const qint64 samples = qint64(audioFrames) * d->samplesPerFrame;
    const int delay = d->handle->encoderDelay;
    const int padding = d->handle->encoderPadding;
    // The decoder's delay is part of the padding (LAME and ffmpeg pad at
    // least 529): of a smaller padding the audio ends past what the decoder
    // gives, so the frames end where its samples end
    const qint64 audio = samples - delay - std::max<qint64>(padding, DecoderDelay);
    if (delay >= 0 && padding >= 0 && audio > 0) {
        m_frames = nframes_t(audio);
        d->skip = nframes_t(delay) + DecoderDelay;
    } else {
        m_frames = nframes_t(samples);
        d->skip = 0;
    }

    // countFrames() read the file to its end and closed it: open again, at
    // frame 0, as a seek puts the decoder anywhere. A decoder that cannot
    // get there does not open (no frames: is_valid() false), with the reason
    initDecoderInternal();
    if (TAudioFileStatus atStart = seek(0); !atStart) {
        m_openError = TAudioFileError{TAudioFileError::Code::CannotOpen, atStart.error().detail};
        m_frames = 0;
    }
}


MadAudioReader::~MadAudioReader()
{
    if (d) {
        if (d->handle) {
            d->handle->cleanup();
            d->handle->clearInputBuffer();
            delete d->handle;
        }
        delete d;
    }
}

bool MadAudioReader::can_decode(TAudioByteSource& source)
{
    //
    // HACK:
    //
    // I am simply no good at this and this detection code is no good as well
    // It always takes waves for mp3 files so we introduce this hack to
    // filter out wave files. :(
    //
    char buffer[12];
    if (!source.seek(0) || source.read(buffer, 12) != 12) {
        return false;
    }
    if (!qstrncmp(buffer, "RIFF", 4) && !qstrncmp(buffer + 8, "WAVE", 4)) {
        return false;
    }


    K3bMad handle;
    if (!handle.open(&source)) {
        return false;
    }
    handle.skipTag();
    if (!handle.seekFirstHeader()) {
        return false;
    }
    if (handle.findNextHeader()) {
        int c = MAD_NCHANNELS(&handle.madFrame->header);
        int layer = handle.madFrame->header.layer;
        unsigned int s = handle.madFrame->header.samplerate;

        //
        // find 4 more mp3 headers (random value since 2 was not enough)
        // This way we get most of the mp3 files while sorting out
        // for example wave files.
        //
        int cnt = 1;
        while (handle.findNextHeader()) {
            // compare the found headers
            if (MAD_NCHANNELS(&handle.madFrame->header) == c &&
                handle.madFrame->header.layer == layer &&
                handle.madFrame->header.samplerate == s) {
                // only support layer III for now since otherwise some wave files
                // are taken for layer I
                if (++cnt >= 5) {
                    //stdout << "(MadDecoder) valid mpeg 1 layer " << layer
                    //<< " file with " << c << " channels and a samplerate of "
                    //<< s << endl;
                    return (layer == MAD_LAYER_III);
                }
            }
            else {
                break;
            }
        }
    }

    return false;
}


TAudioFileStatus MadAudioReader::seek_frame(nframes_t frame)
{
    Q_ASSERT(frame < m_frames);
    if (!d) {
        return seek_failed(frame);
    }

    // The frame in the decoder's output, and the audio frame it is in
    const qint64 decoderFrame = qint64(frame) + d->skip;
    const qint64 target = decoderFrame / d->samplesPerFrame;
    if (target >= d->seekPositions.size()) {
        return seek_failed(frame, QStringLiteral("frame %1 lies past the MPEG frames found").arg(frame));
    }

    // The decoder's state at the target as a read from the start has it:
    // the frames before it decoded first. Their bit reservoir (up to 511
    // bytes, several frames at a low bitrate), the overlap of the previous
    // granule and the synthesis filter's history; the first of them may
    // not decode (their reservoir lies before the seek) and give silence.
    const qint64 first = std::max<qint64>(0, target - PreRollFrames);
    if (!d->handle->inputSeek(qint64(d->seekPositions[first]))) {
        return seek_failed(frame, QStringLiteral("cannot seek in the file to frame %1").arg(frame));
    }
    mad_frame_mute(d->handle->madFrame);
    mad_synth_mute(d->handle->madSynth);
    d->nextFrame = first;
    d->pcmStart = d->pcmCount = 0;
    while (d->nextFrame <= target) {
        if (!decode_next_frame()) {
            return seek_failed(frame, QStringLiteral("the frames before frame %1 do not decode").arg(frame));
        }
    }
    d->pcmStart = std::min(nframes_t(decoderFrame - target * d->samplesPerFrame), d->pcmCount);

    return seek_landed(frame);
}


bool MadAudioReader::initDecoderInternal()
{
    d->handle->cleanup();


    if (!d->handle->open(m_source)) {
        return false;
    }

    if (!d->handle->skipTag()) {
        return false;
    }

    if (!d->handle->seekFirstHeader()) {
        return false;
    }

    return true;
}


unsigned long MadAudioReader::countFrames()
{
    unsigned long frames = 0;
    bool error = false;
    d->vbr = false;
    bool bFirstHeaderSaved = false;

    d->seekPositions.clear();

    while (!error && d->handle->findNextHeader()) {
        if (!bFirstHeaderSaved) {
            bFirstHeaderSaved = true;
            d->firstHeader = d->handle->madFrame->header;
        }
        else if (d->handle->madFrame->header.bitrate != d->firstHeader.bitrate) {
            d->vbr = true;
        }
        //
        // position in stream: position in file minus the not yet used buffer
        //
        unsigned long long seekPos = d->handle->streamPos();

        // save the number of bytes to be read to decode i-1 frames at position i
        // in other words: when seeking to seekPos the next decoded frame will be i
        d->seekPositions.append(seekPos);
    }

    if (!d->handle->inputError() && !error) {
        const unsigned int samplesPerFrame = MAD_NSBSAMPLES(&d->firstHeader) * 32;
        d->samplesPerFrame = (samplesPerFrame > 0 && samplesPerFrame <= 1152) ? samplesPerFrame : 1152;
        frames = d->seekPositions.size();
    }

    d->handle->cleanup();

    return frames;
}


TAudioFileResult MadAudioReader::read_frames(TAudioBuffer& fileIOBuffer, nframes_t count)
{
    if (count == 0) {
        return {};
    }
    fileIOBuffer.check_capacity(count, m_channels);

    const nframes_t readFrames = (d && !at_end() && !m_error) ? decode(fileIOBuffer, count) : 0;

    const bool inputError = d && d->handle->inputError();
    return read_done(fileIOBuffer, readFrames, count, inputError ? QStringLiteral("reading the file failed") : QString());
}


nframes_t MadAudioReader::decode(TAudioBuffer& fileIOBuffer, nframes_t frameCount)
{
    nframes_t given = 0;
    while (given < frameCount && m_frame + given < m_frames) {
        if (d->pcmStart >= d->pcmCount && !decode_next_frame()) {
            break;
        }
        const nframes_t n = std::min({frameCount - given, d->pcmCount - d->pcmStart, m_frames - (m_frame + given)});
        for (uint channel = 0; channel < m_channels; ++channel) {
            std::copy_n(d->pcm[channel] + d->pcmStart, n, fileIOBuffer.get_channel_buffer(channel).data(given, n));
        }
        d->pcmStart += n;
        given += n;
    }
    return given;
}


bool MadAudioReader::decode_next_frame()
{
    if (d->nextFrame >= d->seekPositions.size()) {
        return false;
    }
    K3bMad* mad = d->handle;
    const nframes_t length = d->samplesPerFrame;

    // A frame is the one at its place in the file, not the n-th decode: the
    // first decode after a seek (or at the start) may fail without taking
    // the frame (its bit reservoir is missing) and decode it on the retry,
    // and a damaged frame is taken without audio
    while (true) {
        if (!mad->fillStreamBuffer()) {
            return false;
        }
        const bool decoded = mad_frame_decode(mad->madFrame, mad->madStream) == 0;
        const bool taken = mad->madStream->next_frame > mad->madStream->this_frame;
        if (!decoded && (mad->madStream->error == MAD_ERROR_BUFLEN || !taken)) {
            // The frame is not all in the buffer yet, or junk before it, or
            // a decode that took nothing: once more
            if (!MAD_RECOVERABLE(mad->madStream->error) && mad->madStream->error != MAD_ERROR_BUFLEN) {
                return false;
            }
            continue;
        }

        const qint64 index = frame_index(mad->streamPos());
        if (index < d->nextFrame) {
            // A frame before the one asked for (decoded again)
            continue;
        }
        // A frame the decoder skipped (it found no header there): silence
        // for it, the frame decoded now waits for the next call
        const bool skipped = index > d->nextFrame;

        if (decoded && !skipped) {
            mad_synth_frame(mad->madSynth, mad->madFrame);
            const mad_pcm& out = mad->madSynth->pcm;
            for (uint channel = 0; channel < m_channels; ++channel) {
                // A mono frame in a stereo file goes to both channels
                const mad_fixed_t* samples = out.samples[std::min<uint>(channel, out.channels - 1)];
                const nframes_t n = std::min<nframes_t>(out.length, length);
                for (nframes_t i = 0; i < n; ++i) {
                    d->pcm[channel][i] = float(mad_f_todouble(samples[i]));
                }
                std::fill(d->pcm[channel] + n, d->pcm[channel] + length, 0.0f);
            }
        } else {
            // Taken without audio (damaged), or skipped: silence for it
            for (uint channel = 0; channel < m_channels; ++channel) {
                std::fill_n(d->pcm[channel], length, 0.0f);
            }
            if (skipped && d->nextFrame + 1 < d->seekPositions.size()) {
                // Decode the frame found again when its turn comes (after
                // the last frame there is none: the next call ends)
                mad->inputSeek(qint64(d->seekPositions[d->nextFrame + 1]));
            }
        }
        d->pcmStart = 0;
        d->pcmCount = length;
        ++d->nextFrame;
        return true;
    }
}

qint64 MadAudioReader::frame_index(qint64 position) const
{
    // Mostly the frame asked for
    if (d->nextFrame < d->seekPositions.size() && qint64(d->seekPositions[d->nextFrame]) == position) {
        return d->nextFrame;
    }
    const auto it = std::lower_bound(d->seekPositions.cbegin(), d->seekPositions.cend(), (unsigned long long)position);
    return it - d->seekPositions.cbegin();
}
