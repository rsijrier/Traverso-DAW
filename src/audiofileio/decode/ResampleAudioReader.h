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

#ifndef RESAMPLEAUDIOREADER_H
#define RESAMPLEAUDIOREADER_H

#include <AbstractAudioReader.h>

class PrivateSRC;

class ResampleAudioReader : public AbstractAudioReader
{

public:

    ResampleAudioReader(const QString &filename);
	~ResampleAudioReader();
	
	nframes_t read_from(DecodeBuffer* buffer, nframes_t start, nframes_t count) {
		return AbstractAudioReader::read_from(buffer, start, count);
	}
	nframes_t read_from(DecodeBuffer* buffer, const TTimeRef& start, nframes_t count) {
        return AbstractAudioReader::read_from(buffer, TTimeRef::to_frame(start, m_outputSampleRate), count);
	}
	QString decoder_type() const {return (m_reader) ? m_reader->decoder_type() : "";}
	void clear_buffers();
	
	uint get_output_rate();
	uint get_file_rate();
	int get_convertor_type() const {return m_convertorType;}
	void set_output_rate(uint rate);
    void set_converter_type(int converterType);
	void set_resample_decode_buffer(DecodeBuffer* buffer);

    static int get_default_resample_quality();
	
protected:
	void reset();
	
	bool seek_private(nframes_t start);
	nframes_t read_private(DecodeBuffer* buffer, nframes_t frameCount);
	
	nframes_t resampled_to_file_frame(nframes_t frame);
	nframes_t file_to_resampled_frame(nframes_t frame);
	
	AbstractAudioReader*	m_reader;
    PrivateSRC*             m_privateSRC;
    audio_sample_t**        m_overflowBuffers;
    long                    m_overflowUsed;
    uint                    m_outputSampleRate;
    int                     m_convertorType;
    bool                    m_isResampleAvailable;
    nframes_t               m_readExtraFrames{};
	
private:
	void create_overflow_buffers();
	DecodeBuffer* m_resampleDecodeBuffer;
	bool m_resampleDecodeBufferIsMine;
};

#endif
