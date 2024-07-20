/*
Copyright (C) 2006-2007 Remon Sijrier

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

#include "WriteSource.h"

#include "TExportSpecification.h"
#include <math.h>

#include "AudioBus.h"
#include <AudioDevice.h>
#include "AbstractAudioWriter.h"
#include "Peak.h"
#include "Utils.h"

#include "gdither.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


WriteSource::WriteSource( TExportSpecification* specification )
    : AudioSource(specification->get_export_dir(), specification->get_export_file_name())
    , m_exportSpecification(specification)
{
    m_writer = nullptr;
    m_peak = nullptr;
    m_channelCount = 0;
    m_isRecording = false;
}

WriteSource::~WriteSource()
{
	PENTERDES;
	if (m_peak) {
		delete m_peak;
	}
	
    // If the export state was recording it means ownership of the TExportSpecification
    // was given to this WriteSource instance so we should delete it now
    if (m_exportSpecification->get_recording_state() == TExportSpecification::RecordingState::RECORDING) {
        delete m_exportSpecification;
	}
	if (m_writer) {
		delete m_writer;
	}
}

nframes_t WriteSource::process (nframes_t nframes)
{
    float* writeBuffer = nullptr;
    nframes_t written = 0;
    nframes_t toWrite = 0;
	int cnt = 0;

	// nframes MUST be greater then 0, this is a precondition !
    Q_ASSERT(nframes > 0);
    // No channels why are we processing...
    Q_ASSERT(m_channelCount > 0);

    do {

        /* now do sample rate conversion */

        if (m_sampleRate != m_exportSpecification->get_sample_rate()) {

            int err;

            Q_ASSERT(m_channelCount > 0);

            m_srcData.output_frames = m_outSamplesMax / m_channelCount;
            m_srcData.end_of_input = (m_exportSpecification->get_export_location() + TTimeRef(nframes, m_sampleRate)) >= m_exportSpecification->get_export_end_location();
            m_srcData.data_out = m_dataBuffer;

            if (m_leftOverFrames > 0) {

                /* input data will be in m_leftOverBuffer rather than dataF */

                m_srcData.data_in = m_leftOverBuffer;

                if (cnt == 0) {

                    /* first time, append new data from dataF into the m_leftOverBuffer */

                    memcpy (m_leftOverBuffer + (m_leftOverFrames * m_channelCount), m_exportSpecification->get_render_buffer(), nframes * m_channelCount * sizeof(float));
                    m_srcData.input_frames = nframes + m_leftOverFrames;
                } else {

                    /* otherwise, just use whatever is still left in m_leftoverF; the contents
                    were adjusted using memmove() right after the last SRC call (see
                    below)
                    */

                    m_srcData.input_frames = m_leftOverFrames;
                }
            } else {

                m_srcData.data_in = m_exportSpecification->get_render_buffer();
                m_srcData.input_frames = nframes;

            }

            ++cnt;

            if ((err = src_process (m_srcState, &m_srcData)) != 0) {
                PWARN((QString("an error occured during sample rate conversion: %1").arg(src_strerror(err)).toLatin1().data()));
                return written;
            }

            toWrite = nframes_t(m_srcData.output_frames_gen);
            m_leftOverFrames = nframes_t(m_srcData.input_frames - m_srcData.input_frames_used);

            if (m_leftOverFrames > 0) {
                if (m_leftOverFrames > m_leftOverBufferSize) {
                    PWARN("warning, leftover frames overflowed, glitches might occur in output");
                    m_leftOverFrames = m_leftOverBufferSize;
                }
                memmove (m_leftOverBuffer, (char *) (m_srcData.data_in + (m_srcData.input_frames_used * m_channelCount)),
                     m_leftOverFrames * m_channelCount * sizeof(float));
            }

            writeBuffer = m_dataBuffer;

        } else {

            /* no SRC, keep it simple */

            toWrite = nframes;
            m_leftOverFrames = 0;
            writeBuffer = m_exportSpecification->get_render_buffer();
        }

        if (m_outputData) {
            memset (m_outputData, 0, m_sampleBytes * toWrite * m_channelCount);
        }

        switch (m_exportSpecification->get_data_format()) {
        case SF_FORMAT_PCM_S8:
        case SF_FORMAT_PCM_16:
        case SF_FORMAT_PCM_24:
            for (uint chn = 0; chn < m_channelCount; ++chn) {
                gdither_runf (m_dither, chn, toWrite, writeBuffer, m_outputData);
            }
            /* and export to disk */
            written += m_writer->write(m_outputData, toWrite);
            break;

        case SF_FORMAT_PCM_32:
            for (uint chn = 0; chn < m_channelCount; ++chn) {
                Q_ASSERT(m_outputData);
                int *ob = static_cast<int *>(m_outputData);
                const double int_max = double(INT_MAX);
                const double int_min = double(INT_MIN);

                for (nframes_t x = 0; x < toWrite; ++x) {
                    uint i = chn + (x * m_channelCount);

                    if (writeBuffer[i] > 1.0f) {
                        ob[i] = INT_MAX;
                    } else if (writeBuffer[i] < -1.0f) {
                        ob[i] = INT_MIN;
                    } else {
                        if (writeBuffer[i] >= 0.0f) {
                            ob[i] = lrintf (int_max * writeBuffer[i]);
                        } else {
                            ob[i] = - lrintf (int_min * writeBuffer[i]);
                        }
                    }
                }
            }
            /* and export to disk */
            written += m_writer->write(m_outputData, toWrite);
            break;
        // default is SF_FORMAT_FLOAT
        default:
            // TODO / FIXME
            // we're clipping to max/min 1.0f but do we want this?
            for (nframes_t x = 0; x < toWrite * m_channelCount; ++x) {
                if (writeBuffer[x] > 1.0f) {
                    writeBuffer[x] = 1.0f;
                } else if (writeBuffer[x] < -1.0f) {
                    writeBuffer[x] = -1.0f;
                }
            }
            /* and export to disk */
            written += m_writer->write(writeBuffer, toWrite);
            break;
        }

    } while (m_leftOverFrames >= nframes);

    return written;
}

int WriteSource::prepare_export()
{
	PENTER;
	
    Q_ASSERT(m_exportSpecification->is_valid() == 1);

    // FIXME: review sample rate variables for correctnes
    m_sampleRate = m_exportSpecification->get_sample_rate();
    m_outputRate = audiodevice().get_sample_rate();

    m_channelCount = m_exportSpecification->get_channel_count();
    m_sampleBytes = m_exportSpecification->get_sample_bytes();

    m_processPeaks = false;
    m_dataBuffer = m_leftOverBuffer = nullptr;
    m_leftOverBufferSize = 0;
    m_leftOverFrames = 0;
    m_outSamplesMax = 0;
    m_dither = nullptr;
    m_outputData = nullptr;
    m_srcState = nullptr;

	if (m_writer) {
		delete m_writer;
	}

    set_name(get_name() + m_exportSpecification->get_file_extension());

    m_writer = AbstractAudioWriter::create_audio_writer(m_exportSpecification);
	
    if (!m_writer->open(m_fileName)) {
        PERROR("Write Source failed to open");
		return -1;
	}
	
    if (m_sampleRate != m_exportSpecification->get_sample_rate()) {
		qDebug("Doing samplerate conversion");
		int err;

        if ((m_srcState = src_new (m_exportSpecification->get_sample_rate_conversion_quality(), int(m_channelCount), &err)) == nullptr) {
            PWARN(QString("cannot initialize sample rate conversion: %1").arg(src_strerror(err)).toLatin1().data());
			return -1;
		}

        m_srcData.src_ratio = m_exportSpecification->get_sample_rate() / double(m_sampleRate);
        m_outSamplesMax = nframes_t(ceil (m_exportSpecification->get_block_size() * m_srcData.src_ratio * m_channelCount));
        m_dataBuffer = new audio_sample_t[m_outSamplesMax];

        m_leftOverBufferSize = 4 * m_exportSpecification->get_block_size();
        m_leftOverBuffer = new audio_sample_t[m_leftOverBufferSize * m_channelCount];
        m_leftOverFrames = 0;
	} else {
        m_outSamplesMax = m_exportSpecification->get_block_size() * m_channelCount;
	}

    m_dither = gdither_new (m_exportSpecification->get_dither_type(), m_channelCount, m_exportSpecification->get_dither_size(), m_exportSpecification->get_bit_depth());


    /* allocate buffers where dithering and output will occur */
    if (m_sampleBytes) {
        m_outputData = static_cast<void*>(malloc (m_sampleBytes * m_outSamplesMax));
	}

	return 0;
}


int WriteSource::finish_export( )
{
	PENTER;

	if (m_writer) {
		m_writer->close();
		delete m_writer;
        m_writer = nullptr;
	}
	
    if (m_dataBuffer) {
        delete [] m_dataBuffer;
        m_dataBuffer = nullptr;
    }
    if (m_leftOverBuffer) {
        delete [] m_leftOverBuffer;
        m_leftOverBuffer = nullptr;
    }

	if (m_dither) {
		gdither_free (m_dither);
        m_dither = nullptr;
	}

    if (m_outputData) {
        free (m_outputData);
        m_outputData = nullptr;
	}

    if (m_srcState) {
        src_delete (m_srcState);
        m_srcState = nullptr;
	}

	if (m_peak && m_peak->finish_processing() < 0) {
		PERROR("WriteSource::finish_export : peak->finish_processing() failed!");
	}
		
    // FIXME (?)
    // Be sure to connect to this signal using Qt::queuedConnection!
    // This signal is emited from DiskIO thread!!!!
	emit exportFinished();

	return 1;
}

nframes_t WriteSource::ringbuffer_write(AudioBus* bus, nframes_t nframes, bool realTime)
{
    Q_ASSERT(bus->get_channel_count() == m_channelCount);

    QueueBufferSlot* slot = nullptr;

    if ((slot = dequeue_from_free_queue(realTime)) )
    {
        Q_ASSERT(slot);

        for (uint chan=0; chan < m_channelCount; ++chan) {
            AudioChannel* audioChannel = bus->get_channel(chan);
            Q_ASSERT(audioChannel);

            slot->write_buffer(TTimeRef(), audioChannel->get_buffer(nframes), chan, nframes);
        }

        m_rtBufferSlotsQueue->try_enqueue(slot);

        return slot->get_buffer_size();
    }

    return 0;
}

QueueBufferSlot* WriteSource::dequeue_from_free_queue(bool realTime)
{
    QueueBufferSlot* slot = nullptr;

    if (realTime) {
        if (m_freeBufferSlotsQueue->try_dequeue(slot)) {
            return slot;
        } else {
            // FIXME
            // What about feedback to user that we couldn't
            // write the audiostream to storage media?
            slot = nullptr;
        }
    } else {
        m_freeBufferSlotsQueue->wait_dequeue(slot);
    }

    return slot;
}


void WriteSource::set_process_peaks( bool process )
{
	m_processPeaks = process;
	
	if (!m_processPeaks) {
		return;
	}
	
	Q_ASSERT(!m_peak);
	
	m_peak = new Peak(this);

	if (m_peak->prepare_processing(audiodevice().get_sample_rate()) < 0) {
		PERROR("Cannot process peaks realtime");
		m_processPeaks = false;
		delete m_peak;
        m_peak = nullptr;
		
		return;
	}
}

int WriteSource::rb_file_write(QueueBufferSlot* slot)
{
    nframes_t written = 0;
    uint chan;

    nframes_t nframes = slot->get_buffer_size();
	
    // FIXME make it support any channel count, not just some high enough number?
    audio_sample_t* readbuffer[6];
    for (int index = 0; index < 6; ++index) {
        readbuffer[index] = nullptr;
    }

	for (chan=0; chan<m_channelCount; ++chan) {
		
        readbuffer[chan] = new audio_sample_t[nframes * m_channelCount];

        slot->read_buffer(readbuffer[chan], chan, nframes);

        m_peak->process(chan, readbuffer[chan], nframes);
	}

    if (m_channelCount == 1) {
        m_exportSpecification->set_render_buffer(readbuffer[0]);
    } else {
        // Interlace data into dataF buffer!
        for (uint f=0; f<nframes; f++) {
            for (chan = 0; chan < m_channelCount; chan++) {
                m_exportSpecification->get_render_buffer()[f * m_channelCount + chan] = readbuffer[chan][f];
            }
        }
    }

    written = process(nframes);

    if (written != nframes) {
        // Say something
        PERROR(QString("Different read / write count: read = %1, write = %2").arg(nframes, written))
    }

    for (chan=0; chan<m_channelCount; ++chan) {
        delete [] readbuffer[chan];
    }
	
    return written;
}

void WriteSource::set_recording(bool rec )
{
	m_isRecording = rec;
}

// Called from DiskIO::do_work in DiskAudioThread
// TODO: make sure this function is thread save
void WriteSource::process_realtime_buffers()
{
    m_exportSpecification->set_render_buffer(m_diskIOFramebuffer);

    QueueBufferSlot* slot = nullptr;

    while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
        rb_file_write(slot);
        m_freeBufferSlotsQueue->try_enqueue(slot);
    }

	if (! m_isRecording ) {
		finish_export();
	}
}

BufferStatus* WriteSource::get_buffer_status()
{
    m_bufferstatus.fillStatus = ((m_freeBufferSlotsQueue->size_approx() * 100) / slotcount);
    // FIXME
    // Ugly hack to let DiskIO keep calling process_realtime_buffers()
    // which will then call finish_export()
    if (!m_isRecording) {
        m_bufferstatus.fillStatus = 70;
    }
    m_bufferstatus.set_sync_status(BufferStatus::IN_SYNC);
    return &m_bufferstatus;
}

