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

extern "C" {
#include <mad.h>
}

class K3bMad
{
public:
    K3bMad();
    ~K3bMad();

    // From the start of input, which outlives this
    bool open(TAudioByteSource* input);

    /**
     * @return true if the mad stream contains data
     *         false if there is no data left or an error occurred.
     *         In the latter case inputError() returns true.
     */
    bool fillStreamBuffer();

    /**
     * Skip id3 tags.
     *
     * This will reset the input file.
     */
    bool skipTag();

    /**
     * Find first frame and seek to the beginning of that frame.
     * This is used to skip the junk that many mp3 files start with.
     */
    bool seekFirstHeader();

    bool eof() const;
    bool inputError() const;

    /**
     * Current position in theinput file. This does NOT
     * care about the status of the mad stream. Use streamPos()
     * in that case.
     */
    qint64 inputPos() const;

    /**
     * Current absolut position of the decoder stream.
     */
    qint64 streamPos() const;
    bool inputSeek(qint64 pos);

    void initMad();
    void cleanup();

    bool decodeNextFrame();
    bool findNextHeader();
    bool checkFrameHeader(mad_header* header) const;
    static bool isXingOrInfoFrame(const mad_header* header, const unsigned char* frame_data, size_t frame_len);
    // The encoder's delay and padding from the LAME tag of a Xing/Info
    // frame (LAME and ffmpeg write it); false when it has none
    static bool read_lame_tag(const mad_header* header, const unsigned char* frame_data, size_t frame_len, int& delay, int& padding);

    void createInputBuffer();
    void clearInputBuffer();

    // The gapless information of the file, -1 where it has none
    // (seekFirstHeader() reads it)
    int encoderDelay{-1};
    int encoderPadding{-1};

    mad_stream*   madStream;
    mad_frame*    madFrame;
    mad_synth*    madSynth;
    mad_timer_t*  madTimer;

private:
    TAudioByteSource* m_input{nullptr};
    bool m_madStructuresInitialized;
    unsigned char* m_inputBuffer;
    bool m_bInputError;
    bool m_eofPadded;
    // The zeros behind the file's last byte in the input buffer
    long m_guardBytes{0};

    int m_channels{};
    int m_sampleRate{};
};


class MadAudioReader : public TAudioDecoder
{
public:
    explicit MadAudioReader(const QString& filename, const TAudioSourceOpener& opener = {});
    ~MadAudioReader() override;

    QString decoder_type() const override { return "mad"; }

    static bool can_decode(TAudioByteSource& source);

    TAudioFileStatus seek_frame(nframes_t frame) override;
    TAudioFileResult read_frames(TAudioBuffer& fileIOBuffer, nframes_t count) override;

protected:
    // The decoder's delay in samples (the synthesis filter), as mpg123 and
    // ffmpeg count it for gapless playback
    static constexpr nframes_t DecoderDelay = 529;
    // The frames decoded before the frame a seek goes to, so the decoder's
    // state there is a read from the start's: the bit reservoir reaches up
    // to 511 bytes back, several frames at a low bitrate
    static constexpr qint64 PreRollFrames = 16;

    // frameCount frames from the decoder's position into the channel
    // buffers; the frames it gave
    nframes_t decode(TAudioBuffer& fileIOBuffer, nframes_t frameCount);
    // The next audio frame into the decoder's samples (silence when it
    // cannot be decoded); false at the end of the file
    bool decode_next_frame();
    // The audio frame that starts at position in the file (or the first one
    // after it)
    qint64 frame_index(qint64 position) const;

    bool initDecoderInternal();
    // The audio frames of the file, and where each starts
    unsigned long countFrames();

    class MadDecoderPrivate;
    MadDecoderPrivate* d;
    TAudioByteSource* m_source{nullptr};
};


