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
#include <QFile>
#include <QString>
#include <QVector>

#include "TFileIOBuffer.h"

RELAYTOOL_MAD;

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


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


bool K3bMad::open(const QString& filename)
{
    cleanup();

    m_bInputError = false;
    m_eofPadded = false;
    m_channels = m_sampleRate = 0;

    m_inputFile.setFileName(filename);

    if (!m_inputFile.open(QIODevice::ReadOnly)) {
//		PERROR("could not open file %s", QS_C(filename));
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

        if (!m_inputFile.atEnd()) {
            result = m_inputFile.read((char*)readStart, readSize);
            if (result < 0) {
                m_bInputError = true;
                return false;
            }
        }

        if (result == 0) {
            if (m_eofPadded) {
                return false;
            }
            memset(readStart, 0, MAD_BUFFER_GUARD);
            result = MAD_BUFFER_GUARD;
            m_eofPadded = true;
        }
        else if (m_inputFile.atEnd()) {
            memset(readStart + result, 0, MAD_BUFFER_GUARD);
            result += MAD_BUFFER_GUARD;
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
    m_inputFile.seek(0);

    //
    // now check if the file starts with an id3 tag and skip it if so
    //
    char buf[4096];
    int bufLen = 4096;
    if (m_inputFile.read(buf, bufLen) < bufLen) {
        //kdDebug() << "(K3bMad) unable to read " << bufLen << " bytes from "
        //	      << m_inputFile.name() << endl;
        return false;
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
        if (!m_inputFile.seek(offset)) {
//			PERROR("Couldn't seek to %u in %s", offset, QS_C(m_inputFile.fileName()));
            return false;
        }
    }
    else {
        // reset file
        return m_inputFile.seek(0);
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
       !m_inputFile.atEnd() &&
       streamPos() <= startInputPos+1024) {
        headerFound = findNextHeader();
    }

    if (headerFound) {
        size_t frameLen = madStream->next_frame ? (madStream->next_frame - madStream->this_frame) : 0;
        if (isXingOrInfoFrame(&madFrame->header, madStream->this_frame, frameLen)) {
            // Skip the Xing/Info/VBRI metadata frame so it isn't decoded as silence
            headerFound = findNextHeader();
        }
    }

    // seek back to the begin of the audio frame
    if (headerFound) {
        qint64 targetPos = streamPos();
        m_inputFile.seek(targetPos);
    }

    // reset the stream to make sure mad really starts decoding at our seek position
    mad_stream_finish(madStream);
    mad_stream_init(madStream);
    m_eofPadded = false;

    return headerFound;
}


bool K3bMad::eof() const
{
    return m_inputFile.atEnd();
}


qint64 K3bMad::inputPos() const
{
    return m_inputFile.pos();
}


qint64 K3bMad::streamPos() const
{
    return inputPos() - (madStream->bufend - madStream->this_frame);
}


bool K3bMad::inputSeek(qint64 pos)
{
    mad_stream_finish(madStream);
    mad_stream_init(madStream);
    m_eofPadded = false;
    return m_inputFile.seek(pos);
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
    if (m_inputFile.isOpen()) {
        //kdDebug() << "(K3bMad) cleanup at offset: "
        //	      << "Input file at: " << m_inputFile.pos() << " "
        //	      << "Input file size: " << m_inputFile.size() << " "
        //	      << "stream pos: "
        //	      << ( m_inputFile.pos() - (madStream->bufend - madStream->this_frame + 1) )
        //	      << endl;
        m_inputFile.close();
    }

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
        : overflowBuffers{TAudioBuffer(1152), TAudioBuffer(1152)}
    {
        outputBuffer = nullptr;
        outputPos = 0;
        outputSize = 0;
        overflowSize = 0;
        overflowStart = 0;

        mad_header_init(&firstHeader);
    }

    K3bMad* handle{};
    QVector<unsigned long long> seekPositions;
    bool bOutputFinished{};

    TAudioBuffer* outputBuffer;
    nframes_t	outputPos;
    nframes_t	outputSize;

    TAudioBuffer overflowBuffers[2];
    nframes_t	overflowSize;
    nframes_t	overflowStart;

    mad_header firstHeader{};
    bool vbr{};
};



MadAudioReader::MadAudioReader(const QString& filename)
 : AbstractAudioReader(filename)
{
    d = new MadDecoderPrivate();
    d->handle = new K3bMad();

    initDecoderInternal();

    m_fileFrames = countFrames();

    switch( d->firstHeader.mode ) {
        case MAD_MODE_SINGLE_CHANNEL:
            m_channels = 1;
            break;
        case MAD_MODE_DUAL_CHANNEL:
        case MAD_MODE_JOINT_STEREO:
        case MAD_MODE_STEREO:
            m_channels = 2;
    }

    if (m_fileFrames <= 0) {
        d->handle->cleanup();
        delete d->handle;
        delete d;
        d = nullptr;
        return;
    }

    m_fileSampleRate = d->firstHeader.samplerate;
    m_length = TTimeRef(m_fileFrames, m_fileSampleRate);

    initDecoderInternal();
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

bool MadAudioReader::can_decode(const QString& filename)
{
    if (!libmad_is_present) {
        return false;
    }

    //
    // HACK:
    //
    // I am simply no good at this and this detection code is no good as well
    // It always takes waves for mp3 files so we introduce this hack to
    // filter out wave files. :(
    //
    QFile f(filename);
    if (!f.open( QIODevice::ReadOnly)) {
        return false;
    }

    char buffer[12];
    if (f.read(buffer, 12) != 12) {
        return false;
    }
    if (!qstrncmp(buffer, "RIFF", 4) && !qstrncmp(buffer + 8, "WAVE", 4)) {
        return false;
    }
    f.close();


    K3bMad handle;
    if (!handle.open(filename)) {
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

    //PERROR("unsupported format: %s",QS_C(filename));

    return false;
}


bool MadAudioReader::seek_private(nframes_t start)
{
    Q_ASSERT(d);

    if (start >= m_fileFrames) {
        return false;
    }

    //
    // we need to reset the complete mad stuff
    //
    if (!initDecoderInternal()) {
        return false;
    }

    unsigned int samplesPerFrame = MAD_NSBSAMPLES(&d->firstHeader) * 32;
    if (samplesPerFrame == 0) {
        samplesPerFrame = 1152;
    }

    unsigned int targetFrame = start / samplesPerFrame;
    nframes_t frameOffset = start % samplesPerFrame;

    if (d->seekPositions.isEmpty()) {
        return false;
    }

    if (targetFrame >= static_cast<unsigned int>(d->seekPositions.size())) {
        targetFrame = static_cast<unsigned int>(d->seekPositions.size()) - 1;
    }

    unsigned int frameReservoirProtect = (targetFrame > 3 ? 3 : targetFrame);
    unsigned int startFrame = targetFrame - frameReservoirProtect;

    // seek in the input file to the warmup frame
    d->handle->inputSeek( d->seekPositions[startFrame] );

    // Seek to warmup frame
    d->handle->inputSeek( d->seekPositions[startFrame] );

    // Decode and synthesize warmup frames up to targetFrame
    while (d->handle->streamPos() < static_cast<qint64>(d->seekPositions[targetFrame])) {
        if (!d->handle->decodeNextFrame()) {
            return false;
        }
        mad_synth_frame( d->handle->madSynth, d->handle->madFrame );
    }

    // Reset overflow flags prior to synthesis warm-up
    d->overflowStart = 0;
    d->overflowSize = 0;

    // Direct libmad to decode the target frame entirely into the overflow cache
    d->outputBuffer = nullptr;
    d->outputSize = 0;
    d->outputPos = 0;

    // This populates d->overflowBuffers starting strictly at index 0,
    // and sets d->overflowSize to exactly 1152.
    createPcmSamples(d->handle->madSynth);

    // =========================================================================
    // CORRECTED SEEK OFFSET MATH:
    // If the seek destination lands in the middle of an MP3 frame, we must
    // skip the pre-seek samples. We shift the read pointer (overflowStart) to
    // the frameOffset, and reduce the available size accordingly.
    // =========================================================================
    if (frameOffset < d->overflowSize) {
        d->overflowStart = frameOffset;
        d->overflowSize -= frameOffset;
    } else {
        // Safe fallback if the offset somehow exceeds the decoded frame length
        d->overflowStart = 0;
        d->overflowSize = 0;
    }
    // =========================================================================

    return true;
}


bool MadAudioReader::initDecoderInternal()
{
    d->handle->cleanup();

    d->bOutputFinished = false;

    if (!d->handle->open(m_fileName)) {
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
        unsigned long long seekPos = d->handle->inputPos() -
        (d->handle->madStream->bufend - d->handle->madStream->this_frame);

        // save the number of bytes to be read to decode i-1 frames at position i
        // in other words: when seeking to seekPos the next decoded frame will be i
        d->seekPositions.append(seekPos);
    }

    if (!d->handle->inputError() && !error) {
        unsigned int samplesPerFrame = MAD_NSBSAMPLES(&d->firstHeader) * 32;
        if (samplesPerFrame == 0) {
            samplesPerFrame = 1152;
        }
        frames = d->seekPositions.size() * samplesPerFrame;
    }

    d->handle->cleanup();

    return frames;
}


nframes_t MadAudioReader::read_private(TFileIOBuffer& fileIOBuffer, nframes_t frameCount)
{
    TAudioBuffer &readBuffer = fileIOBuffer.get_file_io_interleaved_buffer();
    d->outputBuffer = &readBuffer;
    d->outputSize = frameCount;
    d->outputPos = 0;

    if (d->overflowSize > 0) {
        nframes_t framesToCopy = std::min(d->overflowSize, frameCount);

        for (uint chan = 0; chan < m_channels; chan++) {
            TAudioBuffer &destination = fileIOBuffer.get_channel_buffer(chan);
            for (nframes_t frame = 0; frame < framesToCopy; ++frame) {
                destination[frame] = d->overflowBuffers[chan][d->overflowStart + frame];
                readBuffer[frame * m_channels + chan] = d->overflowBuffers[chan][d->overflowStart + frame];
            }
        }


        d->outputPos += framesToCopy;

        if (d->overflowSize < frameCount) {
            d->overflowSize = 0;
            d->overflowStart = 0;
        } else {
            d->overflowSize -= frameCount;
            d->overflowStart += frameCount;
            return frameCount;
        }
    }

    bool bOutputBufferFull = false;

    while (!bOutputBufferFull && d->handle->fillStreamBuffer()) {
        if (d->outputPos >= d->outputSize) {
            bOutputBufferFull = true;
        }
        else if (d->handle->decodeNextFrame()) {
            mad_synth_frame(d->handle->madSynth, d->handle->madFrame);

            if (!createPcmSamples(d->handle->madSynth)) {
                PERROR("createPcmSamples");
                return 0;
            }
        }
        else if (d->handle->inputError()) {
            PERROR("inputError");
            return 0;
        }
    }

    nframes_t finalFrames = std::min(d->outputPos, frameCount);
    for (uint chan = 0; chan < m_channels; chan++) {
        TAudioBuffer &destination = fileIOBuffer.get_channel_buffer(chan);
        for (nframes_t frame = 0; frame < finalFrames; ++frame) {
            destination[frame] = readBuffer[frame * m_channels + chan];
        }
    }

    return finalFrames;
}


bool MadAudioReader::createPcmSamples(mad_synth* synth)
{
    TAudioBuffer* writeBuffer = d->outputBuffer;
    nframes_t outputOffset = d->outputPos;
    nframes_t nframes = synth->pcm.length;
    bool overflow = false;
    nframes_t i;
    nframes_t overflowIndex = 0;

    if (writeBuffer && (m_readPos + d->outputPos + nframes) > m_fileFrames) {
        nframes = m_fileFrames - (m_readPos + outputOffset);
    }

    const mad_fixed_t* srcLeft  = synth->pcm.samples[0];
    const mad_fixed_t* srcRight = (synth->pcm.channels == 2) ? synth->pcm.samples[1] : nullptr;

    for (i = 0; i < nframes; i++) {
        if (!overflow && (!writeBuffer || (outputOffset + i >= d->outputSize))) {
            overflow = true;
        }

        /* Process Left Channel */
        if (overflow) {
            d->overflowBuffers[0][overflowIndex] = mad_f_todouble(srcLeft[i]);
        } else if (writeBuffer) {
            (*writeBuffer)[(outputOffset + i) * m_channels] = mad_f_todouble(srcLeft[i]);
        }

        /* Process Right Channel */
        if (srcRight) {
            if (overflow) {
                d->overflowBuffers[1][overflowIndex] = mad_f_todouble(srcRight[i]);
            } else if (writeBuffer) {
                if (m_channels == 2) {
                    (*writeBuffer)[(outputOffset + i) * m_channels + 1] = mad_f_todouble(srcRight[i]);
                }
            }
        }
        /* Mono-to-Stereo Upmix Fallback */
        else if (m_channels == 2 && writeBuffer && !overflow) {
            (*writeBuffer)[(outputOffset + i) * m_channels + 1] = mad_f_todouble(srcLeft[i]);
        } else if (m_channels == 2 && overflow) {
            d->overflowBuffers[1][overflowIndex] = mad_f_todouble(srcLeft[i]);
        }

        if (overflow) {
            overflowIndex++;
        }
    }

    if (overflow) {
        d->overflowSize = overflowIndex;
        d->overflowStart = 0;
    }

    if (writeBuffer) {
        if (outputOffset + i > d->outputSize) {
            d->outputPos = d->outputSize;
        } else {
            d->outputPos += i;
        }
    } else {
        d->outputPos += i;
    }

    return true;
}

