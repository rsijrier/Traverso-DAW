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

#include "FaadAudioReader.h"
#include "TAudioDecoderFactory.h"

#include <QVector>
#include <algorithm>
#include <limits>
#include <optional>

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

extern "C" {
#include <neaacdec.h>
}

static int mp4_read_callback(int64_t offset, void *buffer, size_t size, void *token)
{
	TByteSource *file = static_cast<TByteSource*>(token);
	if (!file || !file->seek(offset)) {
		return 1;
	}
	qint64 bytesRead = file->read(reinterpret_cast<char*>(buffer), size);
	return (bytesRead == static_cast<qint64>(size)) ? 0 : 1;
}

static quint32 be32(const unsigned char* p)
{
	return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

static quint64 be64(const unsigned char* p)
{
	return (quint64(be32(p)) << 32) | be32(p + 4);
}

static constexpr quint32 box_type(const char (&name)[5])
{
	return (quint32(uchar(name[0])) << 24) | (quint32(uchar(name[1])) << 16) | (quint32(uchar(name[2])) << 8) | quint32(uchar(name[3]));
}

// An MP4 box: its type, where its content starts and where it ends
struct Mp4Box
{
	quint32 type{0};
	qint64 content{0};
	qint64 end{0};
};

static bool read_box(TByteSource& file, qint64 at, qint64 limit, Mp4Box& box)
{
	unsigned char header[16];
	if (!file.seek(at) || file.read(reinterpret_cast<char*>(header), 8) != 8) {
		return false;
	}
	quint64 size = be32(header);
	box.type = be32(header + 4);
	qint64 headerSize = 8;
	if (size == 1) {
		if (file.read(reinterpret_cast<char*>(header + 8), 8) != 8) {
			return false;
		}
		size = be64(header + 8);
		headerSize = 16;
	} else if (size == 0) {
		size = quint64(limit - at);
	}
	if (size < quint64(headerSize) || at + qint64(size) > limit) {
		return false;
	}
	box.content = at + headerSize;
	box.end = at + qint64(size);
	return true;
}

static bool find_box(TByteSource& file, qint64 from, qint64 to, quint32 type, Mp4Box& found)
{
	for (qint64 at = from; at < to;) {
		Mp4Box box;
		if (!read_box(file, at, to, box)) {
			return false;
		}
		if (box.type == type) {
			found = box;
			return true;
		}
		at = box.end;
	}
	return false;
}

// The timescale of an mvhd or mdhd box (version 0 or 1)
static quint32 header_timescale(TByteSource& file, const Mp4Box& box)
{
	unsigned char data[24];
	if (!file.seek(box.content) || file.read(reinterpret_cast<char*>(data), 24) != 24) {
		return 0;
	}
	return be32(data + (data[0] == 1 ? 20 : 12));
}

// The gapless information of an M4A: the edit list of its audio track
// (ffmpeg and Apple write one): the priming to skip (media timescale) and
// the length that plays (movie timescale)
struct Mp4Edit
{
	qint64 mediaTime{-1};
	qint64 segmentDuration{-1};
	quint32 movieTimescale{0};
	quint32 mediaTimescale{0};
};

static bool read_audio_edit(TByteSource& file, Mp4Edit& edit)
{
	Mp4Box moov;
	if (!find_box(file, 0, file.size(), box_type("moov"), moov)) {
		return false;
	}
	Mp4Box mvhd;
	if (find_box(file, moov.content, moov.end, box_type("mvhd"), mvhd)) {
		edit.movieTimescale = header_timescale(file, mvhd);
	}
	for (qint64 at = moov.content; at < moov.end;) {
		Mp4Box trak;
		if (!read_box(file, at, moov.end, trak)) {
			return false;
		}
		at = trak.end;
		Mp4Box mdia, hdlr, mdhd, edts, elst;
		if (trak.type != box_type("trak") || !find_box(file, trak.content, trak.end, box_type("mdia"), mdia) ||
		    !find_box(file, mdia.content, mdia.end, box_type("hdlr"), hdlr)) {
			continue;
		}
		unsigned char handler[12];
		if (!file.seek(hdlr.content) || file.read(reinterpret_cast<char*>(handler), 12) != 12 || be32(handler + 8) != box_type("soun")) {
			continue;
		}
		if (find_box(file, mdia.content, mdia.end, box_type("mdhd"), mdhd)) {
			edit.mediaTimescale = header_timescale(file, mdhd);
		}
		if (!find_box(file, trak.content, trak.end, box_type("edts"), edts) ||
		    !find_box(file, edts.content, edts.end, box_type("elst"), elst)) {
			return false;
		}
		// The first edit that plays media (an empty edit, -1, delays the
		// track: not met in files Traverso imports)
		unsigned char head[8];
		if (!file.seek(elst.content) || file.read(reinterpret_cast<char*>(head), 8) != 8) {
			return false;
		}
		const bool wide = head[0] == 1;
		const quint32 entries = be32(head + 4);
		for (quint32 i = 0; i < entries; ++i) {
			unsigned char entry[20];
			const int size = wide ? 20 : 12;
			if (file.read(reinterpret_cast<char*>(entry), size) != size) {
				return false;
			}
			const qint64 duration = wide ? qint64(be64(entry)) : qint64(be32(entry));
			const qint64 mediaTime = wide ? qint64(be64(entry + 8)) : qint64(qint32(be32(entry + 4)));
			if (mediaTime >= 0) {
				edit.segmentDuration = duration;
				edit.mediaTime = mediaTime;
				return edit.movieTimescale > 0 && edit.mediaTimescale > 0;
			}
		}
		return false;
	}
	return false;
}

class FaadAudioReader::FaadDecoderPrivate
{
public:
	FaadDecoderPrivate()
	{
		memset(&demux, 0, sizeof(demux));
	}

	~FaadDecoderPrivate()
	{
		cleanup();
	}

	void close_decoder()
	{
		if (hDecoder) {
			NeAACDecClose(hDecoder);
			hDecoder = nullptr;
		}
	}

	void cleanup()
	{
		close_decoder();

		if (demuxOpen) {
			MP4D_close(&demux);
			memset(&demux, 0, sizeof(demux));
			demuxOpen = false;
			isMp4 = false;
		}

		adtsFrameOffsets.clear();
		adtsFrameSizes.clear();

		sampleCount = 0;
		audioTrack = -1;
	}

	QByteArray readBytes(qint64 offset, int size)
	{
		if (size <= 0) {
			return QByteArray();
		}
		QByteArray bytes(size, Qt::Uninitialized);
		if (!source->seek(offset)) {
			return QByteArray();
		}
		bytes.resize(std::max<qint64>(source->read(bytes.data(), size), 0));
		return bytes;
	}

	// The AAC packets of the file: MP4 samples, or ADTS frames. Nothing when
	// the file gives fewer bytes than the packet has (a file cut short, a
	// read that failed): a read error, not a packet that does not decode (an
	// empty packet is one that does not decode).
	int packet_count() const { return isMp4 ? int(sampleCount) : adtsFrameOffsets.size(); }
	std::optional<QByteArray> packet(int index)
	{
		qint64 offset = 0;
		int bytes = 0;
		if (isMp4) {
			unsigned int size = 0, timestamp = 0, duration = 0;
			offset = qint64(MP4D_frame_offset(&demux, audioTrack, unsigned(index), &size, &timestamp, &duration));
			if (size > unsigned(std::numeric_limits<int>::max())) {
				// No AAC packet is that large: a damaged sample table
				return std::nullopt;
			}
			bytes = int(size);
		} else {
			offset = adtsFrameOffsets[index];
			bytes = adtsFrameSizes[index];
		}
		QByteArray data = readBytes(offset, bytes);
		if (data.size() != qsizetype(std::max(bytes, 0))) {
			return std::nullopt;
		}
		return data;
	}

	TByteSource* source{nullptr};
	NeAACDecHandle hDecoder{nullptr};
	bool isMp4{false};
	bool demuxOpen{false};
	MP4D_demux_t demux;
	int audioTrack{-1};
	unsigned int sampleCount{0};
	// The decoder's setup: the stream's specific config (MP4), or the first
	// ADTS frame
	QByteArray decoderSetup;

	// For ADTS fallback
	QVector<qint64> adtsFrameOffsets;
	QVector<int> adtsFrameSizes;

	// The decoder's model, measured when the file opens: a fresh decoder
	// gives nothing for its first leadingEmpty packets (FAAD: one), every
	// packet after them frameLength frames
	int leadingEmpty{1};
	nframes_t frameLength{1024};
	// Frames of the decoder's output before frame 0 of the file: the
	// priming an edit list says is more than FAAD leaves out by itself
	nframes_t skip{0};

	// The packet decode_next_packet() decodes next, and the frames of the
	// packet decoded last (interleaved), from pendingStart on not read yet
	int nextPacket{0};
	std::vector<float> pending;
	nframes_t pendingStart{0};
	nframes_t pendingCount{0};
};

FaadAudioReader::FaadAudioReader(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
	: TAudioDecoder(fileName, std::move(source), opener)
	, d(new FaadDecoderPrivate())
{
}

std::expected<std::unique_ptr<FaadAudioReader>, TAudioFileError> FaadAudioReader::open(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
{
    return TAudioDecoderFactory::open<FaadAudioReader>(fileName, std::move(source), opener);
}

TAudioFileStatus FaadAudioReader::start()
{
	d->source = &source();
	if (!initDecoderInternal()) {
		d->cleanup();
		return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, m_openFailure.isEmpty() ? QStringLiteral("FAAD cannot read it") : m_openFailure});
	}
	return require_audio();
}

FaadAudioReader::~FaadAudioReader()
{
	delete d;
}

bool FaadAudioReader::can_decode(TByteSource& f)
{
	char header[12];
	f.seek(0);
	qint64 readBytes = f.read(header, 12);
	if (readBytes < 12) {
		return false;
	}

	// Filter out other formats to avoid false positives
	if (!qstrncmp(header, "RIFF", 4) || !qstrncmp(header, "fLaC", 4) ||
	    !qstrncmp(header, "OggS", 4) || !qstrncmp(header, "wvpk", 4)) {
		return false;
	}

	// 1. Check if valid MP4 / M4A container
	f.seek(0);
	MP4D_demux_t demux;
	int mp4Ok = MP4D_open(&demux, mp4_read_callback, &f, f.size());
	if (mp4Ok) {
		bool hasAudio = false;
		for (unsigned int i = 0; i < demux.track_count; ++i) {
			unsigned int obj = demux.track[i].object_type_indication;
			unsigned int handler = demux.track[i].handler_type;
			if (handler == 0x736f756e || // 'soun'
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3 ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_MAIN_PROFILE ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_LC_PROFILE ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_SSR_PROFILE ||
			    demux.track[i].SampleDescription.audio.channelcount > 0) {
				hasAudio = true;
				break;
			}
		}
		MP4D_close(&demux);
		if (hasAudio) {
			return true;
		}
	}

	// 2. Check if valid ADTS AAC stream
	f.seek(0);
	QByteArray buf(4096, Qt::Uninitialized);
	buf.resize(std::max<qint64>(f.read(buf.data(), buf.size()), 0));
	if (buf.size() >= 7) {
		const unsigned char* p = reinterpret_cast<const unsigned char*>(buf.constData());
		if (p[0] == 0xFF && (p[1] & 0xF0) == 0xF0) {
			NeAACDecHandle h = NeAACDecOpen();
			if (h) {
				NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(h);
				config->outputFormat = FAAD_FMT_FLOAT;
				NeAACDecSetConfiguration(h, config);

				unsigned long rate = 0;
				unsigned char ch = 0;
				long initRes = NeAACDecInit(h, const_cast<unsigned char*>(p), buf.size(), &rate, &ch);
				NeAACDecClose(h);
				if (initRes >= 0 && rate > 0 && ch > 0) {
					return true;
				}
			}
		}
	}

	return false;
}

bool FaadAudioReader::initDecoderInternal()
{
	d->cleanup();

	d->source->seek(0);

	// 1. Attempt MP4 demuxing with minimp4
	int mp4Ok = MP4D_open(&d->demux, mp4_read_callback, d->source, d->source->size());
	if (mp4Ok) {
		d->demuxOpen = true;
		for (unsigned int i = 0; i < d->demux.track_count; ++i) {
			unsigned int obj = d->demux.track[i].object_type_indication;
			unsigned int handler = d->demux.track[i].handler_type;
			if (handler == 0x736f756e || // 'soun'
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3 ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_MAIN_PROFILE ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_LC_PROFILE ||
			    obj == MP4_OBJECT_TYPE_AUDIO_ISO_IEC_13818_7_SSR_PROFILE ||
			    d->demux.track[i].SampleDescription.audio.channelcount > 0) {
				d->audioTrack = static_cast<int>(i);
				break;
			}
		}

		if (d->audioTrack >= 0) {
			d->isMp4 = true;
			MP4D_track_t *track = &d->demux.track[d->audioTrack];
			d->sampleCount = track->sample_count;

			d->hDecoder = NeAACDecOpen();
			if (!d->hDecoder) {
				return false;
			}

			NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(d->hDecoder);
			config->outputFormat = FAAD_FMT_FLOAT;
			config->dontUpSampleImplicitSBR = 0;
			NeAACDecSetConfiguration(d->hDecoder, config);

			unsigned long rate = 0;
			unsigned char ch = 0;

			if (track->dsi && track->dsi_bytes > 0) {
				d->decoderSetup = QByteArray(reinterpret_cast<const char*>(track->dsi), int(track->dsi_bytes));
				char initRes = NeAACDecInit2(d->hDecoder, track->dsi, track->dsi_bytes, &rate, &ch);
				if (initRes > 0) {
					return false;
				}
			} else if (d->sampleCount > 0) {
				unsigned int fb = 0, ts = 0, dur = 0;
				MP4D_file_offset_t off = MP4D_frame_offset(&d->demux, d->audioTrack, 0, &fb, &ts, &dur);
				QByteArray firstPkt = d->readBytes(off, fb);
				d->decoderSetup = firstPkt;
				long initRes = NeAACDecInit(d->hDecoder, reinterpret_cast<unsigned char*>(firstPkt.data()), fb, &rate, &ch);
				if (initRes > 0) {
					return false;
				}
			}

			m_rate = static_cast<uint>(rate > 0 ? rate : track->SampleDescription.audio.samplerate_hz);
			m_channels = static_cast<uint>(ch > 0 ? ch : track->SampleDescription.audio.channelcount);

			if (m_rate == 0 || m_channels == 0) {
				return false;
			}

			if (!measure_decoder()) {
				return false;
			}

			// Gapless: the edit list says how much priming to skip and how
			// long the audio is. FAAD leaves out its first packet(s) by
			// itself, as much as most encoders prime (1024 frames); the
			// rest of the priming is skipped here. Without an edit list the
			// track's duration counts the priming FAAD leaves out.
			const qint64 own = qint64(d->leadingEmpty) * d->frameLength;
			const qint64 decodable = qint64(d->packet_count() - d->leadingEmpty) * d->frameLength;
			qint64 frames = decodable;
			Mp4Edit edit;
			const unsigned int timescale = track->timescale ? track->timescale : m_rate;
			const uint64_t duration = ((uint64_t)track->duration_hi << 32) | track->duration_lo;
			if (read_audio_edit(*d->source, edit)) {
				const qint64 priming = edit.mediaTime * qint64(m_rate) / edit.mediaTimescale;
				d->skip = nframes_t(std::max<qint64>(0, priming - own));
				frames = edit.segmentDuration * qint64(m_rate) / edit.movieTimescale;
			} else if (duration > 0) {
				frames = qint64(double(duration) / timescale * m_rate + 0.5) - own;
			}
			m_frames = nframes_t(std::clamp<qint64>(frames, 0, std::max<qint64>(0, decodable - d->skip)));
			// No frames: it cannot be read (and seek() is asked below the
			// frame count only)
			return start_at_frame_0();
		}
	}

	// 2. Fallback: ADTS AAC stream parsing
	d->cleanup();
	d->source->seek(0);

	qint64 fsize = d->source->size();
	qint64 offset = 0;
	while (offset + 7 <= fsize) {
		d->source->seek(offset);
		unsigned char hdr[7];
		if (d->source->read(reinterpret_cast<char*>(hdr), 7) != 7) {
			break;
		}
		if (hdr[0] != 0xFF || (hdr[1] & 0xF0) != 0xF0) {
			offset++;
			continue;
		}
		int frameLen = ((hdr[3] & 0x03) << 11) | (hdr[4] << 3) | ((hdr[5] & 0xE0) >> 5);
		if (frameLen < 7 || offset + frameLen > fsize) {
			offset++;
			continue;
		}
		d->adtsFrameOffsets.append(offset);
		d->adtsFrameSizes.append(frameLen);
		offset += frameLen;
	}

	if (d->adtsFrameOffsets.isEmpty()) {
		return false;
	}

	d->isMp4 = false;
	d->hDecoder = NeAACDecOpen();
	if (!d->hDecoder) {
		return false;
	}

	NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(d->hDecoder);
	config->outputFormat = FAAD_FMT_FLOAT;
	NeAACDecSetConfiguration(d->hDecoder, config);

	QByteArray firstFrame = d->readBytes(d->adtsFrameOffsets[0], d->adtsFrameSizes[0]);
	unsigned long rate = 0;
	unsigned char ch = 0;
	long initRes = NeAACDecInit(d->hDecoder, reinterpret_cast<unsigned char*>(firstFrame.data()), firstFrame.size(), &rate, &ch);
	if (initRes > 0 || rate == 0 || ch == 0) {
		return false;
	}

	m_rate = static_cast<uint>(rate);
	m_channels = static_cast<uint>(ch);
	d->decoderSetup = firstFrame;
	if (!measure_decoder()) {
		return false;
	}
	// ADTS says nothing of priming or padding: the frames FAAD gives (it
	// leaves out as much as most encoders prime), up to an AAC frame more
	// than the encoder was given
	m_frames = nframes_t(qint64(d->packet_count() - d->leadingEmpty) * d->frameLength);
	return start_at_frame_0();
}

// At frame 0, as a read from the start; false (and why) when the file has
// no frames or the decoder cannot get there
bool FaadAudioReader::start_at_frame_0()
{
	if (m_frames == 0) {
		m_openFailure = QStringLiteral("no AAC frames in it");
		return false;
	}
	if (TAudioFileStatus atStart = seek_frame(0); !atStart) {
		m_openFailure = atStart.error().detail;
		return false;
	}
	return true;
}

bool FaadAudioReader::open_decoder()
{
	d->close_decoder();
	d->hDecoder = NeAACDecOpen();
	if (!d->hDecoder) {
		return false;
	}
	NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(d->hDecoder);
	config->outputFormat = FAAD_FMT_FLOAT;
	config->dontUpSampleImplicitSBR = 0;
	NeAACDecSetConfiguration(d->hDecoder, config);

	unsigned long rate = 0;
	unsigned char channels = 0;
	unsigned char* setup = reinterpret_cast<unsigned char*>(d->decoderSetup.data());
	if (d->isMp4 && d->demux.track[d->audioTrack].dsi) {
		return NeAACDecInit2(d->hDecoder, setup, (unsigned long)d->decoderSetup.size(), &rate, &channels) == 0;
	}
	return NeAACDecInit(d->hDecoder, setup, (unsigned long)d->decoderSetup.size(), &rate, &channels) >= 0;
}

bool FaadAudioReader::measure_decoder()
{
	// A fresh decoder over the first packets: how many give nothing, and
	// what the first that gives something gives (1024 frames for AAC LC,
	// 2048 with SBR, at the rate and channels of the decoder's output)
	if (!open_decoder()) {
		return false;
	}
	d->leadingEmpty = 0;
	for (int i = 0; i < std::min(d->packet_count(), 8); ++i) {
		QByteArray data = d->packet(i).value_or(QByteArray());
		NeAACDecFrameInfo info;
		NeAACDecDecode(d->hDecoder, &info, reinterpret_cast<unsigned char*>(data.data()), (unsigned long)data.size());
		if (info.error == 0 && info.samples > 0 && info.channels > 0) {
			d->frameLength = nframes_t(info.samples / info.channels);
			m_channels = info.channels;
			if (info.samplerate > 0) {
				m_rate = uint(info.samplerate);
			}
			return d->frameLength > 0;
		}
		++d->leadingEmpty;
	}
	return false;
}

TAudioFileStatus FaadAudioReader::seek_frame(nframes_t frame)
{
	Q_ASSERT(frame < m_frames);

	// The frame in the decoder's output, and the packet that gives it
	const qint64 decoderFrame = qint64(frame) + d->skip;
	const int target = int(decoderFrame / d->frameLength) + d->leadingEmpty;
	if (target >= d->packet_count()) {
		return seek_failed(frame, QStringLiteral("frame %1 lies past the AAC packets found").arg(frame));
	}

	// A fresh decoder from a few packets before: what it gives for the
	// target is what a read from the start gives there (the overlap of the
	// packet before it is all an AAC packet carries over)
	const int first = std::max(0, target - std::max(PreRollPackets, d->leadingEmpty + 1));
	if (!open_decoder()) {
		return seek_failed(frame, QStringLiteral("FAAD cannot start a decoder"));
	}
	d->nextPacket = first;
	d->pendingStart = d->pendingCount = 0;
	while (d->nextPacket <= target) {
		if (!decode_next_packet()) {
			return seek_failed(frame, QStringLiteral("the packets before frame %1 cannot be read").arg(frame));
		}
	}
	d->pendingStart = std::min(nframes_t(decoderFrame - qint64(target - d->leadingEmpty) * d->frameLength), d->pendingCount);

	return seek_landed(frame);
}

TAudioFileResult FaadAudioReader::read_frames(TAudioBuffer& fileIOBuffer, nframes_t count)
{
    if (count == 0) {
        return {};
    }
    fileIOBuffer.check_capacity(count, m_channels);

    const nframes_t readFrames = (d->hDecoder && !at_end() && !m_inErrorState) ? decode(fileIOBuffer, count) : 0;

    // A packet the file does not give whole (decode_next_packet()); when the
    // packets ran out before the frame count, read_done() says so itself
    const bool packetLeft = d->nextPacket < d->packet_count();
    return read_done(fileIOBuffer, readFrames, count,
                     readFrames < count && packetLeft ? QStringLiteral("the file gives an AAC packet only in part") : QString());
}

nframes_t FaadAudioReader::decode(TAudioBuffer& fileIOBuffer, nframes_t frameCount)
{
	nframes_t given = 0;
	while (given < frameCount && m_nextFrame + given < m_frames) {
		if (d->pendingStart >= d->pendingCount && !decode_next_packet()) {
			break;
		}
		const nframes_t n = std::min({frameCount - given, d->pendingCount - d->pendingStart, m_frames - (m_nextFrame + given)});
		for (uint channel = 0; channel < m_channels; ++channel) {
			audio_sample_t* destination = fileIOBuffer.get_channel_buffer(channel).data(given, n);
			for (nframes_t i = 0; i < n; ++i) {
				destination[i] = d->pending[size_t(d->pendingStart + i) * m_channels + channel];
			}
		}
		d->pendingStart += n;
		given += n;
	}
	return given;
}

bool FaadAudioReader::decode_next_packet()
{
	if (d->nextPacket >= d->packet_count()) {
		return false;
	}
	std::optional<QByteArray> packet = d->packet(d->nextPacket);
	if (!packet) {
		// The file did not give the packet: an error (contract point 4)
		return false;
	}
	QByteArray& data = *packet;
	NeAACDecFrameInfo info;
	void* pcm = NeAACDecDecode(d->hDecoder, &info, reinterpret_cast<unsigned char*>(data.data()), (unsigned long)data.size());
	++d->nextPacket;

	// Every packet counts for frameLength frames, also one that does not
	// decode (silence then), so the packets stay where they are
	const size_t samples = size_t(d->frameLength) * m_channels;
	d->pending.assign(samples, 0.0f);
	if (pcm && info.error == 0 && info.channels == m_channels) {
		std::copy_n(static_cast<const float*>(pcm), std::min<size_t>(info.samples, samples), d->pending.data());
	}
	d->pendingStart = 0;
	d->pendingCount = d->frameLength;
	return true;
}
