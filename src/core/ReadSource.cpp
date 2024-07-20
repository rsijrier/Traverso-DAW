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

#include "ReadSource.h"
#include "ResampleAudioReader.h"

#include "ProjectManager.h"
#include "Project.h"
#include "AudioBus.h"
#include "TLocation.h"
#include "Utils.h"
#include "AudioDevice.h"
#include <QFile>
#include "TConfig.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


/**
 *	\class ReadSource
	\brief A class for (buffered) reading of audio files.
 */


// #define PRINT_BUFFER_STATUS

// This constructor is called for existing (recorded/imported) audio sources
ReadSource::ReadSource(const QDomNode& node)
	: AudioSource()
{
	
	set_state(node);
	
	private_init();
	
	Project* project = pm().get_project();
	
	// FIXME The check below no longer makes sense!!!!!
	// Check if the audiofile exists in our project audiosources dir
	// and give it priority over the dir as given by the project.tpf file
	// This makes it possible to move project directories without Traverso being
	// unable to find it's audiosources!
	if ( QFile::exists(project->get_root_dir() + "/audiosources/" + m_name) || 
	     QFile::exists(project->get_root_dir() + "/audiosources/" + m_name + "-ch0.wav") ) {
		set_dir(project->get_root_dir() + "/audiosources/");
	}
	
	m_silent = (m_channelCount == 0);
}	

// constructor for file import
ReadSource::ReadSource(const QString& dir, const QString& name)
	: AudioSource(dir, name)
{
	private_init();
	
	AbstractAudioReader* reader = AbstractAudioReader::create_audio_reader(m_fileName);

	if (reader) {
		m_channelCount = reader->get_num_channels();
		delete reader;
	} else {
		m_channelCount = 0;
	}

	m_silent = false;
}


// Constructor for recorded audio.
ReadSource::ReadSource(const QString& dir, const QString& name, uint channelCount)
	: AudioSource(dir, name)
{
	private_init();
	
	m_channelCount = channelCount;
	m_silent = false;
	m_name = name  + "-" + QString::number(m_id);
	m_fileName = m_dir + m_name;
	m_rate = pm().get_project()->get_rate();
	m_wasRecording = true;
	m_shortName = m_name.left(m_name.length() - 20);
}


// Constructor for silent clips
ReadSource::ReadSource()
	: AudioSource("", tr("Silence"))
{
	private_init();
	
	m_channelCount = 0;
	m_silent = true;
}


void ReadSource::private_init()
{
    m_location = nullptr;
    m_sourceStartLocation = TTimeRef();

    m_refcount = 0;
	m_error = 0;
    m_resampleAudioReader = nullptr;

    // TODO: make this work
    // used to detect if the transport location comes
    // close to our transport location and the buffers should
    // be filled. Using random number between 0.5 and 3.0 so
    // no all buffers are synced at the same time
    float oneToFourSeconds = 1.0f;
    oneToFourSeconds += ((std::rand() * 3.0) / float(RAND_MAX));
    m_aboutOneToFourSecondsTime = TTimeRef::UNIVERSAL_SAMPLE_RATE * oneToFourSeconds;
    printf("m_aboutHalfToThreeSecondsTime %s\n", QS_C(TTimeRef::timeref_to_ms_2(m_aboutOneToFourSecondsTime)));
}

ReadSource::~ReadSource()
{
	PENTERDES;
	
    if (m_resampleAudioReader) {
        delete m_resampleAudioReader;
    }
}

QDomNode ReadSource::get_state( QDomDocument doc )
{
	QDomElement node = doc.createElement("Source");
	node.setAttribute("channelcount", m_channelCount);
	node.setAttribute("origsheetid", m_origSheetId);
	node.setAttribute("dir", m_dir);
	node.setAttribute("id", m_id);
        node.setAttribute("name", m_name);
	node.setAttribute("origbitdepth", m_origBitDepth);
	node.setAttribute("wasrecording", m_wasRecording);
	node.setAttribute("length", m_length.universal_frame());
	node.setAttribute("rate", m_rate);

	return node;
}


int ReadSource::set_state( const QDomNode & node )
{
	PENTER;
	
	QDomElement e = node.toElement();
    m_channelCount = e.attribute("channelcount", "0").toUInt();
	m_origSheetId = e.attribute("origsheetid", "0").toLongLong();
	set_dir( e.attribute("dir", "" ));
	m_id = e.attribute("id", "").toLongLong();
	m_rate = m_outputRate = e.attribute("rate", "0").toUInt();
	bool ok;
	m_length = TTimeRef(e.attribute("length", "0").toLongLong(&ok));
    m_origBitDepth = e.attribute("origbitdepth", "0").toUInt();
	m_wasRecording = e.attribute("wasrecording", "0").toInt();
	
	// For older project files, this should properly detect if the 
	// audio source was a recording or not., in fact this should suffice
	// and the flag wasrecording would be unneeded, but oh well....
	if (m_origSheetId != 0) {
		m_wasRecording = true;
	}
	
	set_name( e.attribute("name", "No name supplied?" ));
	
	return 1;
}


int ReadSource::init( )
{
	PENTER;
	
	Q_ASSERT(m_refcount);
	
	Project* project = pm().get_project();
	
    m_fileDecodeBuffer = nullptr;
    m_active.store(false);

	// Fake the samplerate, until it's set by an AudioReader!
	if (project) {
		m_rate = m_outputRate = project->get_rate();
	} else {
		m_rate = 44100;
	}
	
	if (m_silent) {
        m_length = TTimeRef::max_length();
		m_channelCount = 0;
		m_origBitDepth = 16;
        m_bufferstatus.fillStatus =  100;
        m_bufferstatus.set_sync_status(BufferStatus::SyncStatus::IN_SYNC);
		return 1;
	}
	
	if (m_channelCount == 0) {
		PERROR("ReadSource channel count is 0");
		return (m_error = INVALID_CHANNEL_COUNT);
	}
	
	if ( ! QFile::exists(m_fileName)) {
		return (m_error = FILE_DOES_NOT_EXIST);
	}

    m_resampleAudioReader = new ResampleAudioReader(m_fileName);
	
    if (!m_resampleAudioReader->is_valid()) {
//		PERROR("ReadSource:: audio reader is not valid! (reader channel count: %d, nframes: %d", m_audioReader->get_num_channels(), m_audioReader->get_nframes());
        delete m_resampleAudioReader;
        m_resampleAudioReader = nullptr;
		return (m_error = COULD_NOT_OPEN_FILE);
	}
	
    int converterType = config().get_property("Conversion", "RTResamplingConverterType", ResampleAudioReader::get_default_resample_quality()).toInt();
    set_output_rate_and_convertor_type(m_resampleAudioReader->get_file_rate(), converterType);
	
    m_channelCount = m_resampleAudioReader->get_num_channels();
	
	// @Ben: I thought we support any channel count now ??
       // if (m_channelCount > 2) {
       //  PERROR(QString("ReadAudioSource: file contains %1 channels; only 2 channels are supported").arg(m_channelCount));
       //         delete m_resampleAudioReader;
       //         m_resampleAudioReader = 0;
       //         return (m_error = INVALID_CHANNEL_COUNT);
       // }

	// Never reached, it's allready checked in AbstractAudioReader::is_valid() which was allready called!
	if (m_channelCount == 0) {
//		PERROR("ReadAudioSource: not a valid channel count: %d", m_channelCount);
        delete m_resampleAudioReader;
        m_resampleAudioReader = nullptr;
		return (m_error = ZERO_CHANNELS);
	}
	
    m_rate = m_resampleAudioReader->get_file_rate();
    m_length = m_resampleAudioReader->get_length();
	
	return 1;
}


void ReadSource::set_output_rate_and_convertor_type(int outputRate, int converterType)
{
    Q_ASSERT(outputRate > 0);
    Q_ASSERT_X(m_resampleAudioReader, "ReadSource::set_output_rate_and_convertor_type", "No Resample Audio Reader");

	bool useResampling = config().get_property("Conversion", "DynamicResampling", true).toBool();
	if (useResampling) {
        m_resampleAudioReader->set_output_rate(outputRate);
	} else {
        m_resampleAudioReader->set_output_rate(m_resampleAudioReader->get_file_rate());
	}

    m_resampleAudioReader->set_converter_type(converterType);

    m_outputRate = outputRate;
	
	// The length could have become slightly smaller/larger due
	// rounding issues involved with converting to one samplerate to another.
	// Should be at the order of one - two samples at most, but for reading purposes we 
	// need sample accurate information!
    m_length = m_resampleAudioReader->get_length();
}

void ReadSource::set_location(TLocation* location)
{
    Q_ASSERT(location);
    m_location = location;
}

void ReadSource::set_source_start_location(const TTimeRef &sourceStartLocation)
{
    // printf("ReadSource::set_source_start_location: %s\n", QS_C(TTimeRef::timeref_to_ms_3(sourceStartLocation)));
    m_sourceStartLocation = sourceStartLocation;
}

int ReadSource::file_read(DecodeBuffer* buffer, const TTimeRef& fileLocation, nframes_t cnt) const
{
    Q_ASSERT(m_resampleAudioReader);
    return m_resampleAudioReader->read_from(buffer, fileLocation, cnt);
}


int ReadSource::file_read(DecodeBuffer * buffer, nframes_t fileLocation, nframes_t cnt)
{
    Q_ASSERT(m_resampleAudioReader);
    return m_resampleAudioReader->read_from(buffer, fileLocation, cnt);
}


ReadSource * ReadSource::deep_copy( )
{
	PENTER;
	
	QDomDocument doc("ReadSource");
	QDomNode rsnode = get_state(doc);
	ReadSource* source = new ReadSource(rsnode);
	return source;
}

nframes_t ReadSource::get_nframes( ) const
{
    if (!m_resampleAudioReader) {
		return 0;
	}
    return m_resampleAudioReader->get_nframes();
}

int ReadSource::set_file(const QString & filename)
{
	PENTER;

	m_error = 0;
	
	int splitpoint = filename.lastIndexOf("/") + 1;
	int length = filename.length();
	
	QString dir = filename.left(splitpoint - 1) + "/";
	QString name = filename.right(length - splitpoint);
		
	set_dir(dir);
	set_name(name);
	
	if (init() < 0) {
		return -1;
	}
	
	emit stateChanged();
	
	return 1;
}


void ReadSource::rb_seek_to_transport_location(const TTimeRef& transportLocation)
{
    // Q_ASSERT(m_bufferstatus.get_sync_status() == BufferStatus::SyncStatus::OUT_OF_SYNC);
    Q_ASSERT(m_location);

    m_bufferstatus.set_sync_status(BufferStatus::QUEUE_SEEKING_TO_NEW_LOCATION);

    if ((transportLocation + m_aboutOneToFourSecondsTime) < m_location->get_start() ||
        transportLocation > m_location->get_end()) {
        m_bufferstatus.set_sync_status(BufferStatus::SyncStatus::OUT_OF_SYNC);
        return;
    }

    TTimeRef seekTransportLocation = transportLocation;
    if (seekTransportLocation < m_location->get_start()) {

        seekTransportLocation = m_location->get_start();

        // printf("transport location before clip start position, adjusting to clip start position %s\n",
        //        QS_C(TTimeRef::timeref_to_ms_3(seekTransportLocation)));
    }

    QueueBufferSlot* slot;
    // The contents of the Slots in the RT queue are most likely useless due to seeking
    // to another transport location so empty the rt queue completely
    // NB: Since we are seeking we are allowed and should clear the rt queue now
    while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
        m_freeBufferSlotsQueue->try_enqueue(slot);
    }

    Q_ASSERT(m_rtBufferSlotsQueue->size_approx() == 0);
    Q_ASSERT(m_freeBufferSlotsQueue->size_approx() == slotcount);

    TTimeRef fileLocation = seekTransportLocation - m_location->get_start() + m_sourceStartLocation;
    printf("rb_seek_to_transport_location: seeking to location transport: %s, file: %s\n",
           QS_C(TTimeRef::timeref_to_ms_3(seekTransportLocation)),
           QS_C(TTimeRef::timeref_to_ms_3(fileLocation)));

    // check if the clip's start position is within the range
    // if not, fill the buffer from the earliest point this clip
    // will come into play.
    if (fileLocation < TTimeRef()) {
        printf("not seeking to file location %s, but to file location %s\n",
               QS_C(TTimeRef::timeref_to_ms_3(fileLocation)), QS_C(TTimeRef::timeref_to_ms_3(m_sourceStartLocation)));
        fileLocation = m_sourceStartLocation;
    }

    m_lastQueuedRTBufferSlot->set_file_location(fileLocation);
    m_bufferstatus.set_sync_status(BufferStatus::QUEUE_SEEKED_TO_NEW_LOCATION);

    process_realtime_buffers();
}

void ReadSource::process_realtime_buffers()
{
    // FIXME: filling still only done on multiples of buffersize
    // however start locations on the time line can start in the middle
    // of a buffer, AudioClip knows this, we don't so in that case a re-sync
    // gets triggered. E.g. play back on different sample rate and the
    // buffer boundaries and the transport location don't line up anymore

    Q_ASSERT(m_lastQueuedRTBufferSlot);
    Q_ASSERT(m_fileDecodeBuffer);
    Q_ASSERT(m_channelCount > 0);

    // printf("ReadSource::fill_realtime_buffers\n");

    auto freeSlots = m_freeBufferSlotsQueue->size_approx();
    if (freeSlots == 0) {
        printf("Free Buffer Slots Queue is empty, why was I called?\n");
        return;
    }

    TTimeRef slotFileLocation = m_lastQueuedRTBufferSlot->get_file_location();
    auto bufferSize = m_lastQueuedRTBufferSlot->get_buffer_size();

    // We need the next slot so add buffer size length to the last slot transport location
    // except when we are seeking, then the rt queueu actually is empty and we need to
    // read to the m_lastQueuedRTBufferSlot->get_transport_location(); since we set that
    // value to the seek transport location
    size_t slotsToFill = freeSlots - 1;  // leave one slot in the rt queue so the ringbuffer_read() Queue Buffer Slot cannot be overwritten by us
    if (m_bufferstatus.get_sync_status() == BufferStatus::QUEUE_SEEKED_TO_NEW_LOCATION) {
        slotsToFill = int(0.7 * slotcount);
    } else {
        slotFileLocation += m_bufferSlotDuration;
    }

    QueueBufferSlot* slot = nullptr;
    nframes_t totalReadSize = slotsToFill * bufferSize;
    // file_read can return 0 in which case m_fileDecodeBuffer internal
    // buffers are the wrong size or not created at all.
    // since we want to fill the rt buffer even beyond the file length
    // for now make sure the decode buffers are the correct size
    m_fileDecodeBuffer->check_buffers_capacity(totalReadSize, m_channelCount);
    nframes_t read = file_read(m_fileDecodeBuffer, slotFileLocation, totalReadSize);
    nframes_t offset = 0;
    if (read != bufferSize) { // likely end of file
        // printf("ReadSource::fill_realtime_buffers: file_read gave only %d\n", read);
    }

    while (slotsToFill)
    {
        if (!m_freeBufferSlotsQueue->try_dequeue(slot)) {
            PERROR("ReadSource::fill_realtime_buffers: try dequeue failed");
            m_bufferstatus.set_sync_status(BufferStatus::FILL_RTBUFFER_DEQUEUE_FAILURE);
            return;
        }

        for (uint chan=0; chan<m_channelCount; ++chan) {
            Q_ASSERT(m_fileDecodeBuffer->destinationBufferSize >= offset+bufferSize);
            // FIXME: use function to get destination buffer that checks if the request is valid
            slot->write_buffer(slotFileLocation, m_fileDecodeBuffer->destination[chan] + offset, chan, bufferSize);
        }

        offset += bufferSize;

        if (!m_rtBufferSlotsQueue->try_enqueue(slot)) {
            PERROR("ReadSource::fill_realtime_buffers: try enqueue failed");
            m_bufferstatus.set_sync_status(BufferStatus::FILL_RTBUFFER_ENQUEUE_FAILURE);
            return;
        }

        slotFileLocation += m_bufferSlotDuration;

        slotsToFill--;
    }

    m_lastQueuedRTBufferSlot = slot;
    Q_ASSERT(m_lastQueuedRTBufferSlot);

    m_bufferstatus.set_sync_status(BufferStatus::SyncStatus::IN_SYNC);
}


nframes_t ReadSource::ringbuffer_read(AudioBus *audioBus, const TTimeRef &fileLocation, nframes_t frames, bool realTime)
{
    if (m_bufferstatus.out_of_sync()) {
        // printf("ReadSource::ringbuffer_read: Buffer out of sync, skipping file location %s\n",
        //        QS_C(TTimeRef::timeref_to_ms_3(fileLocation)));
        return 0;
    }

    QueueBufferSlot* slot = nullptr;

    // auto startTime = TTimeRef::get_nanoseconds_since_epoch();
    nframes_t read = 0;
    auto availableSlots = m_rtBufferSlotsQueue->size_approx();

    while ((slot = dequeue_from_rt_queue(realTime)))
    {
        Q_ASSERT(m_bufferstatus.get_sync_status() != BufferStatus::QUEUE_SEEKING_TO_NEW_LOCATION);

        Q_ASSERT(slot);

        m_freeBufferSlotsQueue->try_enqueue(slot); // always put the dequeued slot on the free slots queue so we don't lose slots

        // check if this slot or any available is a candidate slot, if not, no need to process the
        // whole queue, instead start a resync


        TTimeRef slotFileLocation = slot->get_file_location();

        Q_ASSERT(slotFileLocation != TTimeRef::INVALID);

        if (slotFileLocation == fileLocation)
        {
            for (uint chan=0; chan < m_channelCount; ++chan) {
                slot->read_buffer(audioBus->get_buffer(chan, frames), chan, frames);
            }

            read = slot->get_buffer_size();
            break;
        }

        TTimeRef lastAvailableSlotFileLocation = slotFileLocation + (availableSlots * m_bufferSlotDuration);

        // Check transport location in queue range
        if ((fileLocation < slotFileLocation) || (fileLocation > lastAvailableSlotFileLocation)) {
            printf("ReadSource::ringbuffer_read: FileLocation not in queue range: %s (%s - %s)\n",
                   QS_C(TTimeRef::timeref_to_ms_3(fileLocation)),
                   QS_C(TTimeRef::timeref_to_ms_3(slotFileLocation)),
                   QS_C(TTimeRef::timeref_to_ms_3(lastAvailableSlotFileLocation)));
            m_bufferstatus.set_sync_status(BufferStatus::SyncStatus::OUT_OF_SYNC);
            read = 0;
            break;
        }

        printf("ReadSource::rb_read: Skipping slot %d, location %s\n",
               slot->get_slot_number(), QS_C(TTimeRef::timeref_to_ms_3(slotFileLocation)));
    }

    // auto totalTime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    // printf("ReadSource::rb_read: took nanosecs: %ld\n", totalTime);

    return read;
}

QueueBufferSlot* ReadSource::dequeue_from_rt_queue(bool realTime)
{
    QueueBufferSlot* slot = nullptr;

    if (realTime) {
        if (m_rtBufferSlotsQueue->try_dequeue(slot)) {
            return slot;
        } else {
            // FIXME
            // What about feedback to user that we're missing out on the
            // audio stream?
            slot = nullptr;
        }
    } else {
        m_rtBufferSlotsQueue->wait_dequeue(slot);
    }

    return slot;
}


BufferStatus* ReadSource::get_buffer_status()
{
    Q_ASSERT(m_channelCount > 0);

    if (!m_active.load()) {
        m_bufferstatus.fillStatus =  100;
	} else {
        m_bufferstatus.fillStatus = 100 - ((m_freeBufferSlotsQueue->size_approx() * 100) / slotcount);
	}

    return &m_bufferstatus;
}

void ReadSource::set_active(bool active)
{
    m_active.store(active);
}

uint ReadSource::get_file_rate() const
{
    if (m_resampleAudioReader) {
        return m_resampleAudioReader->get_file_rate();
	} else {
		PERROR("ReadSource::get_file_rate(), but no audioreader available!!");
	}
	
	return pm().get_project()->get_rate(); 
}

void ReadSource::set_decode_buffers(DecodeBuffer* fileDecodeBuffer, DecodeBuffer *resampleDecodeBuffer)
{
    m_fileDecodeBuffer = fileDecodeBuffer;

    if (m_resampleAudioReader) {
        m_resampleAudioReader->set_resample_decode_buffer(resampleDecodeBuffer);
    }
}

QString ReadSource::get_error_string() const
{
	switch(m_error) {
		case COULD_NOT_OPEN_FILE: return tr("Could not open file");
		case INVALID_CHANNEL_COUNT: return tr("Invalid channel count");
		case ZERO_CHANNELS: return tr("File has zero channels");
		case FILE_DOES_NOT_EXIST: return tr("The file does not exist!");
	}
	return tr("No ReadSource error set");
}

