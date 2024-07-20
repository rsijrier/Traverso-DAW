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

#ifndef MADAUDIOREADER_H
#define MADAUDIOREADER_H

#include <AbstractAudioReader.h>

extern "C" {
#include <mad.h>
}

#include <QFile>

class K3bMad
{
public:
    K3bMad();
    ~K3bMad();

    bool open(const QString& filename);

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

    void createInputBuffer();
    void clearInputBuffer();

    mad_stream*   madStream;
    mad_frame*    madFrame;
    mad_synth*    madSynth;
    mad_timer_t*  madTimer;

private:
    QFile m_inputFile;
    bool m_madStructuresInitialized;
    unsigned char* m_inputBuffer;
    bool m_bInputError;

    int m_channels{};
    uint m_sampleRate{};
};


class MadAudioReader : public AbstractAudioReader
{
public:
	MadAudioReader(const QString& filename);
	~MadAudioReader();

        void init();
	
	QString decoder_type() const {return "mad";}
	void clear_buffers();
	
	static bool can_decode(const QString& filename);
	
protected:
	bool seek_private(nframes_t start);
	nframes_t read_private(DecodeBuffer* buffer, nframes_t frameCount);

	void create_buffers();
	bool initDecoderInternal();
	unsigned long countFrames();
	bool createPcmSamples(mad_synth* synth);
	
	static int	MaxAllowedRecoverableErrors;

	class MadDecoderPrivate;
	MadDecoderPrivate* d;

private:
        void private_cleanup();
        void private_clear_buffers();
};

#endif
