/*
Copyright (C) 2007 Ben Levitt

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
#include "SFAudioReader.h"
#include "WPAudioReader.h"

#include "Utils.h"

#include <QString>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


AbstractAudioReader::AbstractAudioReader(const QString& filename)
{
    m_fileName = filename;
    m_readPos = m_channels = m_fileFrames = 0;
    m_fileSampleRate = 0;
    m_length = TTimeRef();
}


AbstractAudioReader::~AbstractAudioReader()
= default;


// Read cnt frames starting at start from the AudioReader, into dst
// uses seek() and read() from AudioReader subclass
nframes_t AbstractAudioReader::read_from(DecodeBuffer* buffer, nframes_t start, nframes_t count)
{
// 	printf("read_from:: before_seek from %d, framepos is %d\n", start, m_readPos);

    if (!seek(start)) {
        return 0;
    }

    return read(buffer, count);
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


nframes_t AbstractAudioReader::read(DecodeBuffer* buffer, nframes_t count)
{
    if (count && m_readPos < m_fileFrames) {

        // Make sure the read buffer is big enough for this read
        buffer->check_buffers_capacity(count, m_channels);

        // printf("read_from:: after_seek from %d, framepos is %d\n", start, m_readPos);
        nframes_t framesRead = read_private(buffer, count);

        m_readPos += framesRead;

        return framesRead;
    }

    return 0;
}


// Static method used by other classes to get an AudioReader for the correct file type
AbstractAudioReader* AbstractAudioReader::create_audio_reader(const QString& filename)
{
    AbstractAudioReader* newReader = nullptr;

    if (SFAudioReader::can_decode(filename)) {
        newReader = new SFAudioReader(filename);
    } else if (WPAudioReader::can_decode(filename)) {
        newReader = new WPAudioReader(filename);
    } else {
        // Audio Format not supported by sndfile and not a wavpack
        PERROR(QString("File format not supported %1").arg(filename));
    }


    if (newReader && !newReader->is_valid()) {
//        PERROR("new %s reader is invalid! (channels: %d, frames: %d)", QS_C(newReader->decoder_type()), newReader->get_num_channels(), newReader->get_nframes());
        delete newReader;
        newReader = nullptr;
    }

    return newReader;
}

DecodeBuffer::DecodeBuffer()
{
    destination = nullptr;
    readBuffer = nullptr;
    m_channels = destinationBufferSize = readBufferSize = 0;
}


void DecodeBuffer::check_buffers_capacity(uint size, uint channels)
{
/*	m_bufferSizeCheckCounter++;
    m_totalCheckSize += size;

    float meanvalue = (m_totalCheckSize / (float)m_bufferSizeCheckCounter);

    if (meanvalue < destinationBufferSize && ((meanvalue + 256) < destinationBufferSize) && !(destinationBufferSize == size)) {
        m_smallerReadCounter++;
        if (m_smallerReadCounter > 8) {
            delete_destination_buffers();
            delete_readbuffer();
            m_bufferSizeCheckCounter = m_smallerReadCounter = 0;
            m_totalCheckSize = 0;
        }
    }*/


    if (destinationBufferSize < size || m_channels < channels) {

        delete_destination_buffers();

        m_channels = channels;

        destination = new audio_sample_t*[m_channels];

        for (uint chan = 0; chan < m_channels; chan++) {
            destination[chan] = new audio_sample_t[size];
        }

        destinationBufferSize = size;
// 		printf("resizing destination to %.3f KB\n", (float)size*4/1024);
    }

    if (readBufferSize < (size*m_channels)) {

        delete_readbuffer();

        readBuffer = new audio_sample_t[size*m_channels];
        readBufferSize = (size*m_channels);
    }
}

void DecodeBuffer::delete_destination_buffers()
{
    if (destination) {
        for (uint chan = 0; chan < m_channels; chan++) {
            delete [] destination[chan];
        }

        delete [] destination;

        destination = nullptr;
        destinationBufferSize = 0;
        m_channels = 0;
    }
}

void DecodeBuffer::delete_readbuffer()
{
    if (readBuffer) {

        delete [] readBuffer;

        readBuffer = nullptr;
        readBufferSize = 0;
    }
}
