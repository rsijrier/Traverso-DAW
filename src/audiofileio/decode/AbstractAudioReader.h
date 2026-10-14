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

#pragma once

#include "TTimeRef.h"
#include "defines.h"
#include "TFileIOBuffer.h"

#include <QString>

class AbstractAudioReader
{
	
public:
	AbstractAudioReader(const QString& filename);
	virtual ~AbstractAudioReader();
	
    uint get_num_channels();
    TTimeRef get_length() const {return m_length;}
    nframes_t get_nframes() const {return m_fileFrames;}
    uint get_file_rate();
	bool eof();
	nframes_t pos();

    nframes_t read_from(TFileIOBuffer &fileIOBuffer, nframes_t start, nframes_t count);
	bool seek(nframes_t start);
    nframes_t read(TFileIOBuffer &buffer, nframes_t frameCount);
	
    bool is_valid() {return (m_channels > 0 && m_fileFrames > 0);}
	virtual QString decoder_type() const = 0;
	virtual void clear_buffers() {}
	
    static std::unique_ptr<AbstractAudioReader> create_audio_reader(const QString& filename);

    // True if one of the readers can decode filename. The file is only probed,
    // no reader or other resources are kept, and nothing is logged.
    static bool can_decode(const QString& filename);
	
protected:
	enum class Decoder {
		None,
		SndFile,
		WavPack,
		Faad,
		Mad
	};

	// The reader that can decode filename, tried in order of preference
	static Decoder find_decoder(const QString& filename);

	virtual bool seek_private(nframes_t start) = 0;
    virtual nframes_t read_private(TFileIOBuffer &buffer, nframes_t frameCount) = 0;
	
	QString		m_fileName;

	nframes_t	m_readPos;
    uint		m_channels;
    TTimeRef	m_length;
    nframes_t	m_fileFrames;
    uint		m_fileSampleRate;
};

