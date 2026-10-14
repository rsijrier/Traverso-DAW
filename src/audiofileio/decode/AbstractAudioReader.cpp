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

#include "AbstractAudioReader.h"
#include "TFileIOBuffer.h"

#include "SFAudioReader.h"
#include "WPAudioReader.h"
#include "MadAudioReader.h"

#if defined M4A_DECODE_SUPPORT
#include "FaadAudioReader.h"
#endif

#include <QString>

RELAYTOOL_FAAD;

#include "Debugger.h"


AbstractAudioReader::AbstractAudioReader(const QString& filename)
{
    (void)libfaad_is_present;
    m_fileName = filename;
    m_readPos = m_channels = m_fileFrames = 0;
    m_fileSampleRate = 0;
    m_length = TTimeRef();
}


AbstractAudioReader::~AbstractAudioReader()
    = default;


// Read cnt frames starting at start from the AudioReader, into dst
// uses seek() and read() from AudioReader subclass
nframes_t AbstractAudioReader::read_from(TFileIOBuffer &fileIOBuffer, nframes_t start, nframes_t count)
{
    // 	printf("read_from:: before_seek from %d, framepos is %d\n", start, m_readPos);

    if (!seek(start)) {
        return 0;
    }

    return read(fileIOBuffer, count);
}


uint AbstractAudioReader::get_num_channels()
{
    return m_channels;
}


uint AbstractAudioReader::get_file_rate()
{
    return m_fileSampleRate;
}


bool AbstractAudioReader::eof()
{
    return (m_readPos >= m_fileFrames);
}


nframes_t AbstractAudioReader::pos()
{
    return m_readPos;
}


bool AbstractAudioReader::seek(nframes_t start)
{
    PENTER3;
    if (m_readPos != start) {
        if (!seek_private(start)) {
            return false;
        }
        m_readPos = start;
    }

    return true;
}


nframes_t AbstractAudioReader::read(TFileIOBuffer &buffer, nframes_t count)
{
    if (count > 0 && m_readPos < m_fileFrames) {

        // Make sure the read buffer is big enough for this read
        buffer.check_capacity(count, m_channels);
        // and contains only zero's
        buffer.silence_buffers();

        // printf("read_from:: after_seek from %d, framepos is %d\n", start, m_readPos);
        nframes_t framesRead = read_private(buffer, count);

        m_readPos += framesRead;

        return framesRead;
    }

    return 0;
}


AbstractAudioReader::Decoder AbstractAudioReader::find_decoder(const QString& filename)
{
    if (SFAudioReader::can_decode(filename)) {
        return Decoder::SndFile;
    }
    if (WPAudioReader::can_decode(filename)) {
        return Decoder::WavPack;
    }
#if defined M4A_DECODE_SUPPORT
    if (FaadAudioReader::can_decode(filename)) {
        return Decoder::Faad;
    }
#endif
    if (MadAudioReader::can_decode(filename)) {
        return Decoder::Mad;
    }
    return Decoder::None;
}

bool AbstractAudioReader::can_decode(const QString& filename)
{
    return find_decoder(filename) != Decoder::None;
}

// Static method used by other classes to get an AudioReader for the correct file type
std::unique_ptr<AbstractAudioReader> AbstractAudioReader::create_audio_reader(const QString& filename)
{
    std::unique_ptr<AbstractAudioReader> newReader;

    switch (find_decoder(filename)) {
    case Decoder::SndFile:
        newReader = std::unique_ptr<AbstractAudioReader>(new SFAudioReader(filename));
        break;
    case Decoder::WavPack:
        newReader = std::unique_ptr<AbstractAudioReader>(new WPAudioReader(filename));
        break;
    case Decoder::Faad:
#if defined M4A_DECODE_SUPPORT
        newReader = std::unique_ptr<AbstractAudioReader>(new FaadAudioReader(filename));
#endif
        break;
    case Decoder::Mad:
        newReader = std::unique_ptr<AbstractAudioReader>(new MadAudioReader(filename));
        break;
    case Decoder::None:
        // Audio Format not supported by sndfile and not a wavpack
        PERROR(QString("File format not supported %1").arg(filename));
        break;
    }


    if (newReader && !newReader->is_valid()) {
        PERROR(QString("new %1 reader is invalid! (channels: %2, frames: %3)").
               arg(newReader->decoder_type().arg(newReader->get_num_channels()).arg(newReader->get_nframes())));
    }

    return newReader;
}
