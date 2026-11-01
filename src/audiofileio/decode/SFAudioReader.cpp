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

#include "SFAudioReader.h"
#include "TAudioDecoderFactory.h"
#include <QString>

#include <algorithm>
#include <vector>

#include "TAudioBuffer.h"
#include "TLog.h"


// libsndfile's virtual IO on a byte source

static sf_count_t source_length(void* source)
{
    return static_cast<TByteSource*>(source)->size();
}

static sf_count_t source_seek(sf_count_t offset, int whence, void* userData)
{
    TByteSource* source = static_cast<TByteSource*>(userData);
    const qint64 position = whence == SEEK_SET ? offset : whence == SEEK_CUR ? source->position() + offset : source->size() + offset;
    return source->seek(position) ? position : -1;
}

static sf_count_t source_read(void* data, sf_count_t bytes, void* source)
{
    // A failure stays with the source, which the decoder asks: to libsndfile
    // it is a short read
    return std::max<sf_count_t>(static_cast<TByteSource*>(source)->read(data, bytes), 0);
}

static sf_count_t source_tell(void* source)
{
    return static_cast<TByteSource*>(source)->position();
}

static SNDFILE* open_on_source(TByteSource& source, SF_INFO& info)
{
    static SF_VIRTUAL_IO io{source_length, source_seek, source_read, nullptr, source_tell};
    source.seek(0);
    return sf_open_virtual(&io, SFM_READ, &info, &source);
}


SFAudioReader::SFAudioReader(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
    : TAudioDecoder(fileName, std::move(source), opener)
{
}

std::expected<std::unique_ptr<SFAudioReader>, TAudioFileError> SFAudioReader::open(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
{
    return TAudioDecoderFactory::open<SFAudioReader>(fileName, std::move(source), opener);
}

TAudioFileStatus SFAudioReader::start()
{
    /* although libsndfile says we don't need to set this,
	valgrind and source code shows us that we do.
	Really? Look it up !
	*/
    memset (&m_sfinfo, 0, sizeof(m_sfinfo));

    if ((m_sf = open_on_source(source(), m_sfinfo)) == nullptr) {
        // libsndfile's last open error is global: with files opened at the
        // same time on other threads it may be theirs (task T26)
        return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, QString::fromUtf8(sf_strerror(nullptr))});
    }

    m_channels = m_sfinfo.channels;
    m_frames = m_sfinfo.frames;
    m_rate = m_sfinfo.samplerate;
    return require_audio();
}


SFAudioReader::~SFAudioReader()
{
    if (m_sf) {
        if (sf_close(m_sf)) {
            TLOG_W("audiofile", "{}: closing it failed", m_fileName.toStdString());
        }
    }
}


std::expected<int, QString> SFAudioReader::format(TByteSource& source)
{
    SF_INFO infos;

    /* although libsndfile says we don't need to set this,
	valgrind and source code shows us that we do.
	*/
    memset (&infos, 0, sizeof(infos));

    SNDFILE* sndfile = open_on_source(source, infos);

    //is it supported by libsndfile?
    if (!sndfile) {
        return std::unexpected(QString::fromUtf8(sf_strerror(nullptr)));
    }

    sf_close(sndfile);

    return infos.format;
}


TAudioFileStatus SFAudioReader::seek_frame(nframes_t frame)
{
    Q_ASSERT(frame < m_frames);
    if (!m_sf) {
        return seek_failed(frame);
    }

    // libsndfile's Ogg seek is exact from the start of the file only: from
    // another position some seeks land a few hundred frames off (libsndfile
    // 1.2.2, test_audio_decoders). Back to the start first; a seek there is
    // cheap, the seek from it is a bisection of the file either way.
    if ((m_sfinfo.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_OGG && sf_seek(m_sf, 0, SEEK_SET) < 0) {
        return seek_failed(frame, QString::fromUtf8(sf_strerror(m_sf)));
    }

    if (sf_seek (m_sf, (off_t) frame, SEEK_SET) < 0) {
        return seek_failed(frame, QString::fromUtf8(sf_strerror(m_sf)));
    }

    return seek_landed(frame);
}


// libsndfile's interleaved frames before they go to the channels: one per
// thread, used within one read only (sf_readf_float() calls nothing of ours
// back), grown to the largest read on the thread
static float* interleaved_scratch(size_t samples)
{
    static thread_local std::vector<float> scratch;
    if (scratch.size() < samples) {
        scratch.resize(samples);
    }
    return scratch.data();
}

TAudioFileResult SFAudioReader::read_frames(TAudioBuffer& fileIOBuffer, nframes_t count)
{
    if (count == 0) {
        return {};
    }
    fileIOBuffer.check_capacity(count, m_channels);

    float* readBuffer = interleaved_scratch(size_t(count) * m_channels);

    const nframes_t readFrames = (m_sf && !at_end() && !m_inErrorState) ? nframes_t(sf_readf_float(m_sf, readBuffer, count)) : 0;

    // De-interlace
    switch (m_channels) {
    case 1:
    {
        std::copy_n(readBuffer, readFrames, fileIOBuffer.get_channel_buffer(0).get_data(readFrames));
        break;
    }
    case 2:
    {
        audio_sample_t* left = fileIOBuffer.get_channel_buffer(0).get_data(readFrames);
        audio_sample_t* right = fileIOBuffer.get_channel_buffer(1).get_data(readFrames);
        for (nframes_t frame = 0; frame < readFrames; frame++) {
            int index = frame*2;
            left[frame] = readBuffer[index];
            right[frame] = readBuffer[index + 1];
        }
        break;
    }
    default:
    {
        for (uint channel = 0; channel < m_channels; channel++) {
            TChannelBuffer &destBuffer = fileIOBuffer.get_channel_buffer(channel);
            for (nframes_t frame = 0; frame < readFrames; frame++) {
                destBuffer[frame] = readBuffer[frame * m_channels + channel];
            }
        }
    }
    }

    // libsndfile says why when it knows (a codec error); a file that ends
    // early it does not
    const bool libraryError = m_sf && sf_error(m_sf) != SF_ERR_NO_ERROR;
    return read_done(fileIOBuffer, readFrames, count, libraryError ? QString::fromUtf8(sf_strerror(m_sf)) : QString());
}

