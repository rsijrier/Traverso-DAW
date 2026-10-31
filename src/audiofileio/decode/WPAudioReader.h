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
#include "wavpack/wavpack.h"

class WPAudioReader : public TAudioDecoder
{
public:
    static std::expected<std::unique_ptr<WPAudioReader>, TAudioFileError> open(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener);
    ~WPAudioReader() override;

    QString decoder_type() const override { return "wavpack"; }

    static bool can_decode(TByteSource& source);

    static void print_libwavpack_version();

    TAudioFileStatus seek_frame(nframes_t frame) override;
    TAudioFileResult read_frames(TAudioBuffer& fileIOBuffer, nframes_t count) override;

private:
    WPAudioReader(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener);
    TAudioFileStatus start();

    // readFrames frames WavPack unpacked into the channel buffers, as float
    void unpack(TAudioBuffer& fileIOBuffer, const int32_t* unpackBuffer, nframes_t readFrames);

    WavpackContext*	m_wp{nullptr};
    bool		m_isFloat{false};
    int		m_bitsPerSample{0};
    int		m_bytesPerSample{0};
};

