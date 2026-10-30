/*
Copyright (C) 2026 Traverso Team

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
#include "TAudioBuffer.h"

class FaadAudioReader : public TAudioDecoder
{
public:
	explicit FaadAudioReader(const QString& filename, const TByteSourceOpener& opener = {});
	~FaadAudioReader() override;

    QString decoder_type() const final { return "faad"; }

	static bool can_decode(TByteSource& source);

    TAudioFileStatus seek_frame(nframes_t frame) override;
    TAudioFileResult read_frames(TAudioBuffer& fileIOBuffer, nframes_t count) override;

protected:
    // The packets decoded before the one a seek goes to (with a fresh
    // decoder), so it gives what a read from the start gives there
    static constexpr int PreRollPackets = 2;

    // frameCount frames from the decoder's position into the channel
    // buffers; the frames it gave
    nframes_t decode(TAudioBuffer& fileIOBuffer, nframes_t frameCount);
    // The next packet into the decoder's output (silence when it does not
    // decode); false at the end of the file
    bool decode_next_packet();
    // A fresh FAAD decoder, set up for the file
    bool open_decoder();
    // How a fresh decoder starts: its empty packets, its frames per packet
    bool measure_decoder();
    // At frame 0 after the open; false, with the reason, when not
    bool start_at_frame_0();

	bool initDecoderInternal();

	class FaadDecoderPrivate;
	FaadDecoderPrivate* d;
};

