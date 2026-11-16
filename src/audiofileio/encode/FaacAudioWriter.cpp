/*
Copyright (C) 2026 Ben Levitt
Copyright (C) 2026 Traverso Authors

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

#include "FaacAudioWriter.h"
#include <stdio.h>
#include <faac.h>

#include <QString>
#include <QtGlobal>

#include <cerrno>
#include <cstring>
#include <tuple>


struct FaacAudioWriter::FaacInfo {
#if defined(FAAC_VERSION_MAJOR) && (FAAC_VERSION_MAJOR >= 1)
	faac_encoder* encoder;
	uint32_t frameSamples;
	uint32_t maxOutputBytes;
#else
	faacEncHandle handle;
	unsigned long inputSamples;
	unsigned long maxOutputBytes;
#endif
};


FaacAudioWriter::FaacAudioWriter(const TAudioEncodeSettings& settings)
	: TAudioEncoder(settings)
{
	m_fid = nullptr;
	m_faacInfo = new FaacInfo();
#if defined(FAAC_VERSION_MAJOR) && (FAAC_VERSION_MAJOR >= 1)
	m_faacInfo->encoder = nullptr;
	m_faacInfo->frameSamples = 1024;
	m_faacInfo->maxOutputBytes = 8192;
#else
	m_faacInfo->handle = nullptr;
	m_faacInfo->inputSamples = 0;
	m_faacInfo->maxOutputBytes = 0;
#endif
	m_buffer = nullptr;
	m_bufferSize = 0;

	// Default settings
	m_bitrate = 192;
	m_quality = 100;
	m_useQuality = false;
	m_objectType = 2; // AAC-LC (LOW)
	m_useTns = 1;
	m_useMidside = 1;
}


FaacAudioWriter::~FaacAudioWriter()
{
	// TAudioWriter closes it and reports how that went; this is the last
	// resort for an encoder destroyed with its file open
	std::ignore = FaacAudioWriter::close();
	if (m_buffer) {
		delete [] m_buffer;
	}
	delete m_faacInfo;
}


TAudioFileStatus FaacAudioWriter::open(const QString& fileName)
{
	std::ignore = close();
	QString why;
	m_fid = fopen(fileName.toUtf8().data(), "wb+");
	if (!m_fid) {
		return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, QString::fromUtf8(std::strerror(errno))});
	}

#if defined(FAAC_VERSION_MAJOR) && (FAAC_VERSION_MAJOR >= 1)
	faac_params params;
#if FAAC_VERSION_MAJOR >= 2
	faac_status st = faac_params_init(&params, sizeof(params));
#else
	faac_status st = faac_params_init(&params);
#endif
	if (st != FAAC_OK) {
		why = QStringLiteral("AAC: faac_params_init failed: %1").arg(QString::fromUtf8(faac_strerror(st)));
		fclose(m_fid);
		m_fid = nullptr;
		return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, why});
	}

	params.sample_rate = m_settings.sampleRate;
	params.num_channels = m_settings.channelCount;
	params.mpeg_version = FAAC_MPEG4;
	params.object_type = (m_objectType == 5) ? FAAC_OBJ_HE_AAC_V1 : FAAC_OBJ_LOW;
	params.joint_mode = (params.num_channels > 1 && m_useMidside) ? FAAC_JOINT_MS : FAAC_JOINT_NONE;
	params.use_tns = (m_useTns != 0);
	params.output_format = FAAC_STREAM_ADTS;
	params.input_format = FAAC_INPUT_16BIT;

	if (m_useQuality && m_quality > 0) {
		params.quant_quality = m_quality;
		params.bit_rate = 0;
	} else {
		uint32_t kbps = (m_bitrate > 0) ? m_bitrate : 128;
		params.bit_rate = (kbps * 1000) / params.num_channels;
		params.quant_quality = 0;
	}

	st = faac_encoder_open(&params, &m_faacInfo->encoder);
	if (st != FAAC_OK || !m_faacInfo->encoder) {
		why = QStringLiteral("AAC: faac_encoder_open failed: %1").arg(QString::fromUtf8(faac_strerror(st)));
		fclose(m_fid);
		m_fid = nullptr;
		return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, why});
	}

	faac_encoder_info info;
	info.struct_size = sizeof(info);
	st = faac_encoder_get_info(m_faacInfo->encoder, &info);
	if (st != FAAC_OK) {
		why = QStringLiteral("AAC: faac_encoder_get_info failed: %1").arg(QString::fromUtf8(faac_strerror(st)));
		faac_encoder_close(&m_faacInfo->encoder);
		fclose(m_fid);
		m_fid = nullptr;
		return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, why});
	}

	m_faacInfo->frameSamples = info.frame_samples;
	m_faacInfo->maxOutputBytes = info.max_output_bytes;
#else
	m_faacInfo->handle = faacEncOpen(m_settings.sampleRate, m_settings.channelCount, &m_faacInfo->inputSamples, &m_faacInfo->maxOutputBytes);
	if (!m_faacInfo->handle) {
		why = QStringLiteral("AAC: faacEncOpen failed");
		fclose(m_fid);
		m_fid = nullptr;
		return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, why});
	}

	faacEncConfigurationPtr cfg = faacEncGetCurrentConfiguration(m_faacInfo->handle);
	if (!cfg) {
		why = QStringLiteral("AAC: faacEncGetCurrentConfiguration failed");
		faacEncClose(m_faacInfo->handle);
		m_faacInfo->handle = nullptr;
		fclose(m_fid);
		m_fid = nullptr;
		return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, why});
	}

	cfg->aacObjectType = (m_objectType == 5) ? 5 : LOW;
	cfg->mpegVersion = MPEG4;
	cfg->useTns = m_useTns ? 1 : 0;
	cfg->allowMidside = (m_settings.channelCount > 1 && m_useMidside) ? 1 : 0;
	cfg->outputFormat = 1; // 1 = ADTS
	cfg->inputFormat = FAAC_INPUT_16BIT;

	if (m_useQuality && m_quality > 0) {
		cfg->quantqual = m_quality;
		cfg->bitRate = 0;
	} else {
		uint32_t kbps = (m_bitrate > 0) ? m_bitrate : 128;
		cfg->bitRate = (kbps * 1000) / m_settings.channelCount;
		cfg->quantqual = 0;
	}

	if (!faacEncSetConfiguration(m_faacInfo->handle, cfg)) {
		why = QStringLiteral("AAC: faacEncSetConfiguration failed");
		faacEncClose(m_faacInfo->handle);
		m_faacInfo->handle = nullptr;
		fclose(m_fid);
		m_fid = nullptr;
		return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, why});
	}
#endif

	return {};
}


TAudioFileResult FaacAudioWriter::write(void* buffer, nframes_t frameCount)
{
	if (frameCount == 0) {
		return {};
	}
	if (!m_fid || !buffer) {
		return {0, TAudioFileError{TAudioFileError::Code::WriteFailed, QStringLiteral("no file open")}};
	}

	if (m_bufferSize < (long)m_faacInfo->maxOutputBytes) {
		delete [] m_buffer;
		m_bufferSize = m_faacInfo->maxOutputBytes;
		m_buffer = new char[m_bufferSize];
	}

	const int16_t* pcmData = static_cast<const int16_t*>(buffer);
	uint32_t totalSamples = (uint32_t)frameCount * m_settings.channelCount;

#if defined(FAAC_VERSION_MAJOR) && (FAAC_VERSION_MAJOR >= 1)
	uint32_t maxChunkSamples = m_faacInfo->frameSamples * m_settings.channelCount;
	uint32_t samplesProcessed = 0;

	while (samplesProcessed < totalSamples) {
		uint32_t chunk = qMin(totalSamples - samplesProcessed, maxChunkSamples);
		uint32_t bytesWritten = 0;
		faac_status status = faac_encoder_encode(m_faacInfo->encoder,
							 pcmData + samplesProcessed,
							 chunk,
							 reinterpret_cast<uint8_t*>(m_buffer),
							 m_bufferSize,
							 &bytesWritten);
		if (status < 0) {
			return {0, TAudioFileError{TAudioFileError::Code::WriteFailed, QStringLiteral("AAC: faac_encoder_encode failed: %1").arg(QString::fromUtf8(faac_strerror(status)))}};
		}

		if (bytesWritten > 0) {
			if (fwrite(m_buffer, 1, bytesWritten, m_fid) != bytesWritten) {
				return {0, TAudioFileError{TAudioFileError::Code::WriteFailed, QStringLiteral("AAC: fwrite failed (%1)").arg(QString::fromUtf8(std::strerror(errno)))}};
			}
		}

		samplesProcessed += chunk;
	}
#else
	unsigned long maxChunkSamples = m_faacInfo->inputSamples;
	unsigned long samplesProcessed = 0;

	while (samplesProcessed < totalSamples) {
		unsigned long chunk = qMin((unsigned long)(totalSamples - samplesProcessed), maxChunkSamples);
		int bytesWritten = faacEncEncode(m_faacInfo->handle,
						(int32_t*)(pcmData + samplesProcessed),
						chunk,
						reinterpret_cast<unsigned char*>(m_buffer),
						m_bufferSize);
		if (bytesWritten < 0) {
			return {0, TAudioFileError{TAudioFileError::Code::WriteFailed, QStringLiteral("AAC: faacEncEncode failed")}};
		}

		if (bytesWritten > 0) {
			if (fwrite(m_buffer, 1, bytesWritten, m_fid) != (size_t)bytesWritten) {
				return {0, TAudioFileError{TAudioFileError::Code::WriteFailed, QStringLiteral("AAC: fwrite failed (%1)").arg(QString::fromUtf8(std::strerror(errno)))}};
			}
		}

		samplesProcessed += chunk;
	}
#endif

	return {frameCount, std::nullopt};
}


TAudioFileStatus FaacAudioWriter::close()
{
	if (!m_fid) {
		return {};
	}
	std::optional<TAudioFileError> error;

	if (m_bufferSize < (long)m_faacInfo->maxOutputBytes) {
		delete [] m_buffer;
		m_bufferSize = m_faacInfo->maxOutputBytes;
		m_buffer = new char[m_bufferSize];
	}

#if defined(FAAC_VERSION_MAJOR) && (FAAC_VERSION_MAJOR >= 1)
	if (m_faacInfo->encoder) {
		uint32_t bytesWritten = 0;
		do {
			bytesWritten = 0;
			faac_status status = faac_encoder_encode(m_faacInfo->encoder,
								 nullptr,
								 0,
								 reinterpret_cast<uint8_t*>(m_buffer),
								 m_bufferSize,
								 &bytesWritten);
			if (status < 0) {
				error = TAudioFileError{TAudioFileError::Code::WriteFailed, QStringLiteral("AAC: faac_encoder_encode flush failed: %1").arg(QString::fromUtf8(faac_strerror(status)))};
				break;
			}
			if (bytesWritten > 0) {
				if (fwrite(m_buffer, 1, bytesWritten, m_fid) != bytesWritten) {
					error = TAudioFileError{TAudioFileError::Code::WriteFailed, QStringLiteral("AAC: flush fwrite failed (%1)").arg(QString::fromUtf8(std::strerror(errno)))};
					break;
				}
			}
		} while (bytesWritten > 0);

		faac_encoder_close(&m_faacInfo->encoder);
		m_faacInfo->encoder = nullptr;
	}
#else
	if (m_faacInfo->handle) {
		int bytesWritten = 0;
		do {
			bytesWritten = faacEncEncode(m_faacInfo->handle,
						     nullptr,
						     0,
						     reinterpret_cast<unsigned char*>(m_buffer),
						     m_bufferSize);
			if (bytesWritten < 0) {
				error = TAudioFileError{TAudioFileError::Code::WriteFailed, QStringLiteral("AAC: faacEncEncode flush failed")};
				break;
			}
			if (bytesWritten > 0) {
				if (fwrite(m_buffer, 1, bytesWritten, m_fid) != (size_t)bytesWritten) {
					error = TAudioFileError{TAudioFileError::Code::WriteFailed, QStringLiteral("AAC: flush fwrite failed (%1)").arg(QString::fromUtf8(std::strerror(errno)))};
					break;
				}
			}
		} while (bytesWritten > 0);

		faacEncClose(m_faacInfo->handle);
		m_faacInfo->handle = nullptr;
	}
#endif

	// stdio writes its last bytes here: a full disk shows now
	if (m_fid) {
		if (fclose(m_fid) != 0 && !error) {
			error = TAudioFileError{TAudioFileError::Code::WriteFailed, QString::fromUtf8(std::strerror(errno))};
		}
		m_fid = nullptr;
	}

	if (error) {
		return std::unexpected(std::move(*error));
	}
	return {};
}
