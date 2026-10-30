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

#pragma once

#include "TAudioDecoder.h"
#include "sndfile.h"


// WAV, W64, AIFF, FLAC, Ogg Vorbis, MP3 and what else libsndfile reads
class SFAudioReader : public TAudioDecoder
{
public:
    explicit SFAudioReader(const QString& filename, const TAudioSourceOpener& opener = {});
    ~SFAudioReader() override;

    QString decoder_type() const override { return "sndfile"; }

    // libsndfile's major format of the file (SF_FORMAT_WAV, ...), 0 when it
    // cannot read it
    static int major_format(TAudioByteSource& source);

    TAudioFileStatus seek_frame(nframes_t frame) override;
    TAudioFileResult read_frames(TAudioBuffer& fileIOBuffer, nframes_t count) override;

private:
    SNDFILE*	m_sf{nullptr};
    SF_INFO		m_sfinfo{};
};

