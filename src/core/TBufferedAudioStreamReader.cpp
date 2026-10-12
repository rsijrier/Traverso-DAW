/*
Copyright (C) 2006-2024 Remon Sijrier

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

#include "TBufferedAudioStreamReader.h"
#include "ResampleAudioReader.h"

#include "TProjectManager.h"
#include "TProject.h"
#include "AudioBus.h"
#include "TFileIOBuffer.h"
#include "TQueueBufferSlot.h"
#include "Utils.h"
#include "TAudioDevice.h"
#include "TConfig.h"
#include "Debugger.h"

#include <QFile>
#include <QThread>

/**
 *	\class ReadSource
    \brief A class for (buffered) reading of audio files.
 */


// #define PRINT_BUFFER_STATUS

// This constructor is called for existing (recorded/imported) audio sources
TBufferedAudioStreamReader::TBufferedAudioStreamReader(const QDomNode& node)
    : TBufferedAudioStream()
{

    set_state(node);

    private_init();

    TProject* project = pm().get_project();

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
TBufferedAudioStreamReader::TBufferedAudioStreamReader(const QString& dir, const QString& name)
    : TBufferedAudioStream(dir, name)
{
    private_init();

    std::unique_ptr<AbstractAudioReader> reader = AbstractAudioReader::create_audio_reader(m_fileName);

    if (reader) {
        m_channelCount = reader->get_num_channels();
    } else {
        m_channelCount = 0;
    }

    m_silent = false;
}


// Constructor for recorded audio.
TBufferedAudioStreamReader::TBufferedAudioStreamReader(const QString& dir, const QString& name, uint channelCount)
    : TBufferedAudioStream(dir, name)
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
TBufferedAudioStreamReader::TBufferedAudioStreamReader()
    : TBufferedAudioStream("", tr("Silence"))
{
    private_init();

    m_channelCount = 0;
    m_silent = true;
}


void TBufferedAudioStreamReader::private_init()
{
    m_sourceStartLocation = TTimeRef();

    m_refcount = 0;
    m_error = 0;
    m_resampleAudioReader = nullptr;

    m_aboutOneToFourSecondsTime = TTimeRef::UNIVERSAL_SAMPLE_RATE * (TraversoDAW::Utils::randomNumberBetween(0, 3) + 1.0);
}

TBufferedAudioStreamReader::~TBufferedAudioStreamReader()
{
    PENTERDES;

    if (m_resampleAudioReader) {
        delete m_resampleAudioReader;
    }
}

QDomNode TBufferedAudioStreamReader::get_state( QDomDocument doc )
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


int TBufferedAudioStreamReader::set_state( const QDomNode & node )
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


int TBufferedAudioStreamReader::init( )
{
    PENTER;

    Q_ASSERT(m_refcount);

    TProject* project = pm().get_project();
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
        m_bufferstatus.set_fill_status(100);
        m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::SyncStatus::IN_SYNC);
        return 1;
    }

    if (m_channelCount == 0) {
        PERROR("ReadSource channel count is 0");
        return (m_error = INVALID_CHANNEL_COUNT);
    }

    if ( ! QFile::exists(m_fileName)) {
        return (m_error = FILE_DOES_NOT_EXIST);
    }

    m_resampleAudioReader = new TResampleAudioReader(m_fileName);

    if (!m_resampleAudioReader->is_valid()) {
//		PERROR("ReadSource:: audio reader is not valid! (reader channel count: %d, nframes: %d", m_audioReader->get_num_channels(), m_audioReader->get_nframes());
        delete m_resampleAudioReader;
        m_resampleAudioReader = nullptr;
        return (m_error = COULD_NOT_OPEN_FILE);
    }

    int converterType = config().get_property("Conversion", "RTResamplingConverterType", TResampleAudioReader::get_default_resample_quality()).toInt();
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


void TBufferedAudioStreamReader::set_output_rate_and_convertor_type(int outputRate, int converterType)
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

void TBufferedAudioStreamReader::set_source_start_location(const TTimeRef &sourceStartLocation)
{
    // printf("ReadSource::set_source_start_location: %s\n", QS_C(TTimeRef::timeref_to_ms_3(sourceStartLocation)));
    m_sourceStartLocation = sourceStartLocation;
}

int TBufferedAudioStreamReader::file_read(TFileIOBuffer& buffer, const TTimeRef& fileLocation, nframes_t cnt) const
{
    Q_ASSERT(m_resampleAudioReader);
    return m_resampleAudioReader->read_from(buffer, fileLocation, cnt);
}


int TBufferedAudioStreamReader::file_read(TFileIOBuffer& buffer, nframes_t fileLocation, nframes_t cnt) const
{
    Q_ASSERT(m_resampleAudioReader);
    return m_resampleAudioReader->read_from(buffer, fileLocation, cnt);
}


TBufferedAudioStreamReader * TBufferedAudioStreamReader::deep_copy( )
{
    PENTER;

    QDomDocument doc("ReadSource");
    QDomNode rsnode = get_state(doc);
    TBufferedAudioStreamReader* source = new TBufferedAudioStreamReader(rsnode);
    return source;
}

nframes_t TBufferedAudioStreamReader::get_nframes( ) const
{
    if (!m_resampleAudioReader) {
        return 0;
    }
    return m_resampleAudioReader->get_nframes();
}

int TBufferedAudioStreamReader::set_file(const QString & filename)
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


void TBufferedAudioStreamReader::rb_seek_to_transport_location(TFileIOBuffer &fileDecodeBuffer, const TTimeRef& transportLocation)
{
    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::QUEUE_SEEKING_TO_NEW_LOCATION);

    // If the transport location lies (much) in front of our start location
    // or after our end location no need to fill the buffers
    if ((transportLocation + m_aboutOneToFourSecondsTime) < m_streamStart ||
        transportLocation > m_streamEnd) {
        m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::SyncStatus::OUT_OF_SYNC);
        return;
    }

    // Transport Location is in our range or at least close. since we want to start to fill
    // the buffers in advance the transport location can still be in front of our start location
    // In which case we set the seek transport location to our start location
    TTimeRef seekTransportLocation = transportLocation;
    if (seekTransportLocation < m_streamStart) {
        seekTransportLocation = m_streamStart;

        // printf("transport location before clip start position, adjusting to clip start position %s\n",
        //        QS_C(TTimeRef::timeref_to_ms_3(seekTransportLocation)));
    }

    TQueueBufferSlot* slot;
    // The contents of the Slots in the RT queue are most likely useless due to seeking
    // to another transport location so empty the rt queue completely
    // NB: Since we are seeking we are allowed and should clear the rt queue now
    while (m_rtBufferSlotsQueue->try_dequeue(slot)) {
        m_freeBufferSlotsQueue->try_enqueue(slot);
    }

    // check if the queue's are still valid and no slots were lost somwhere in the process
    Q_ASSERT(m_rtBufferSlotsQueue->size_approx() == 0);
    Q_ASSERT(m_freeBufferSlotsQueue->size_approx() == m_slotcount);

    // Since we can represent a 'view' of a complete audiofile, always add the source start
    // location to the seek transport location to file location calculation.
    TTimeRef fileLocation = seekTransportLocation - m_streamStart + m_sourceStartLocation;
    // printf("ReadSource::rb_seek_to_transport_location: seeking to location transport: %s, file: %s\n",
    //        QS_C(TTimeRef::timeref_to_ms_3(transportLocation)),
    //        QS_C(TTimeRef::timeref_to_ms_3(fileLocation)));


    // Since the seek transport location and our own start location don't have to align
    // in multiples of buffer slot duration, check if this is true and align the file location
    // accordingly
    TTimeRef timeDiff = seekTransportLocation - transportLocation;
    TTimeRef modulus = TTimeRef(timeDiff.universal_frame() % m_bufferSlotDuration.universal_frame());
    nframes_t bufferSize = audiodevice().get_buffer_size();
    // if modules != 0 then the seek transport location is not a multiple of transport location + x * buffer slot duration
    if (modulus != TTimeRef()) {
        // effectively this means that the start location of a buffer and the corresponding file location becomes negative
        fileLocation -= modulus;
        // keep track of the seek transport location as well, probably not usefull to keep track of 2 locations in slot buffers?
        seekTransportLocation -= modulus;

        // convert the modulus to frames
        nframes_t offset = TTimeRef::to_frame(modulus, m_outputRate);
        // printf("file location after adjustment %s, offset nframes %d\n", QS_C(TTimeRef::timeref_to_ms_3(fileLocation)), offset);

        // and only read in the amount of frames needed for this buffer slot
        nframes_t toRead = bufferSize - offset;

        fileDecodeBuffer.check_capacity(toRead, m_channelCount);
        fileDecodeBuffer.silence_buffers();

        // and read in the samples. We have to use the source start location as the start location, see explanation above
        nframes_t read = file_read(fileDecodeBuffer, m_sourceStartLocation, toRead);
        if (read != toRead) {
            printf("Could not read %d frames, only %d\n", toRead, read);
        }

        if (!m_freeBufferSlotsQueue->try_dequeue(slot)) {
            printf("ReadSource::rb_seek_to_transport_location:: try dequeue failed");
            m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::FILL_RTBUFFER_DEQUEUE_FAILURE);
            return;
        }

        for (uint chan=0; chan<m_channelCount; ++chan) {
            // and now write it into the buffer using the offset
            slot->write_buffer(seekTransportLocation, fileLocation, fileDecodeBuffer.get_channel_buffer(chan).get_data(toRead), chan, toRead, offset);
        }

        if (!m_rtBufferSlotsQueue->try_enqueue(slot)) {
            printf("ReadSource::fill_realtime_buffers: try enqueue failed");
            m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::FILL_RTBUFFER_ENQUEUE_FAILURE);
            return;
        }

        fileLocation = m_sourceStartLocation + m_bufferSlotDuration - modulus;
        seekTransportLocation += m_bufferSlotDuration;
    }

    m_lastQueuedRTBufferSlot->set_file_location(fileLocation);
    m_lastQueuedRTBufferSlot->set_transport_location(seekTransportLocation);
    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::QUEUE_SEEKED_TO_NEW_LOCATION);

    process_realtime_buffers(fileDecodeBuffer);
}

void TBufferedAudioStreamReader::process_realtime_buffers(TFileIOBuffer& fileDecodeBuffer)
{
    Q_ASSERT(m_lastQueuedRTBufferSlot);
    Q_ASSERT(m_channelCount > 0);

    // printf("ReadSource::fill_realtime_buffers\n");

    auto freeSlots = m_freeBufferSlotsQueue->size_approx();
    if (freeSlots == 0) {
        printf("Free Buffer Slots Queue is empty, why was I called?\n");
        return;
    }

    TTimeRef slotFileLocation = m_lastQueuedRTBufferSlot->get_file_location();
    TTimeRef transportLocation = m_lastQueuedRTBufferSlot->get_transport_location();
    auto bufferSize = m_lastQueuedRTBufferSlot->get_buffer_size();

    // We need the next slot so add buffer size length to the last slot transport location
    // except when we are seeking, then the rt queueu actually is empty and we need to
    // read to the m_lastQueuedRTBufferSlot->get_transport_location(); since we set that
    // value to the seek transport location
    size_t slotsToFill = freeSlots;
    if (m_bufferstatus.get_sync_status() == TBufferedAudioStreamStatus::QUEUE_SEEKED_TO_NEW_LOCATION) {
        slotsToFill = int(0.7 * m_slotcount);
    } else {
        slotFileLocation += m_bufferSlotDuration;
        transportLocation += m_bufferSlotDuration;
    }

    TQueueBufferSlot* slot = nullptr;
    nframes_t totalReadSize = slotsToFill * bufferSize;
    // file_read can return 0 in which case m_fileDecodeBuffer internal
    // buffers are the wrong size or not created at all.
    // since we want to fill the rt buffer even beyond the file length
    // for now make sure the decode buffers are the correct size
    fileDecodeBuffer.check_capacity(totalReadSize, m_channelCount);
    nframes_t read = file_read(fileDecodeBuffer, slotFileLocation, totalReadSize);
    nframes_t offset = 0;
    if (read != bufferSize) { // likely end of file
        // printf("ReadSource::fill_realtime_buffers: file_read gave only %d\n", read);
    }

    while (slotsToFill)
    {
        if (!m_freeBufferSlotsQueue->try_dequeue(slot)) {
            PERROR("ReadSource::fill_realtime_buffers: try dequeue failed");
            m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::FILL_RTBUFFER_DEQUEUE_FAILURE);
            return;
        }

        for (uint chan=0; chan<m_channelCount; ++chan) {
            slot->write_buffer(transportLocation, slotFileLocation, fileDecodeBuffer.get_channel_buffer(chan).get_data(totalReadSize) + offset, chan, bufferSize);
        }

        offset += bufferSize;

        if (!m_rtBufferSlotsQueue->try_enqueue(slot)) {
            PERROR("ReadSource::fill_realtime_buffers: try enqueue failed");
            m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::FILL_RTBUFFER_ENQUEUE_FAILURE);
            return;
        }

        slotFileLocation += m_bufferSlotDuration;
        transportLocation += m_bufferSlotDuration;

        slotsToFill--;
    }

    m_lastQueuedRTBufferSlot = slot;
    Q_ASSERT(m_lastQueuedRTBufferSlot);

    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::SyncStatus::IN_SYNC);
}


nframes_t TBufferedAudioStreamReader::ringbuffer_read(TProcessCallBackData &processData, const TTimeRef &fileLocation)
{
    if (m_bufferstatus.out_of_sync()) {
        if (processData.get_is_real_time()) {
            printf("ReadSource::ringbuffer_read: RealTime: Buffer out of sync, skipping file location %s\n",
                   QS_C(TTimeRef::timeref_to_ms_3(fileLocation)));
            printf("ReadSource::ringbuffer_read: RealTime: Buffer status %s\n", QS_C(m_bufferstatus.get_readable_sync_status()));
            return 0;
        } else {
            // During freewheeling the status of the buffers can get out of sync
            // Reason: Buffers get only filled when the transport location get's close to the start
            // of an AudioClip, about 3-4 seconds the buffers get filled. If the DiskIO thread
            // is slow or overloaded compared to the audio thread, the latter one can get in front of the disk thread
            // It's perfectly valid and in fact mandatory to start waiting here to let the disk thread
            // fill the buffers and then continue freewheeling
            printf("ReadSource::ringbuffer_read: FreeWheeling: Buffer status %s, waiting for buffers to get back in sync\n", QS_C(m_bufferstatus.get_readable_sync_status()));
            uint counter = 0;
            while(m_bufferstatus.out_of_sync() && (counter < 100000)) {
                QThread::sleep(std::chrono::nanoseconds(10000));
                counter++;
            }
            if (m_bufferstatus.out_of_sync()) {
                printf("ReadSource::ringbuffer_read: FreeWheeling: Buffers still out of sync after 1 second wait, giving up\n");
                return 0;
            } else {
                printf("ReadSource::ringbuffer_read: Buffers back in sync after %d micro second, continuing processing now\n", counter);
            }
        }
    }

    TQueueBufferSlot* slot = nullptr;

    // auto startTime = TTimeRef::get_nanoseconds_since_epoch();
    nframes_t read = 0;
    nframes_t nframes = processData.get_nframes_to_process();
    AudioBus* bus = processData.get_ringbuffer_read_bus();


    auto availableSlots = m_rtBufferSlotsQueue->size_approx();

    while ((slot = dequeue_from_rt_queue(processData)))
    {
        Q_ASSERT(m_bufferstatus.get_sync_status() != TBufferedAudioStreamStatus::QUEUE_SEEKING_TO_NEW_LOCATION);
        Q_ASSERT(m_bufferstatus.get_sync_status() != TBufferedAudioStreamStatus::QUEUE_ABOUT_TO_BE_DELETED);
        Q_ASSERT(slot);

        TTimeRef slotFileLocation = slot->get_file_location();

        Q_ASSERT(slotFileLocation != TTimeRef::INVALID);

        if (slotFileLocation == fileLocation)
        {
            for (uint chan=0; chan < m_channelCount; ++chan) {
                slot->read_buffer(bus->get_buffer(chan), chan, nframes);
            }

            read = slot->get_read_nframes();

            m_freeBufferSlotsQueue->try_enqueue(slot); // always put the dequeued slot on the free slots queue so we don't lose slots
            break;
        }

        TTimeRef lastAvailableSlotFileLocation = slotFileLocation + (availableSlots * m_bufferSlotDuration);

        // check if this slot or any available is a candidate slot, if not, no need to process the
        // whole queue, instead start a resync
        if ((fileLocation < slotFileLocation) || (fileLocation > lastAvailableSlotFileLocation)) {
            printf("ReadSource::ringbuffer_read: FileLocation not in queue range: %s (%s - %s)\n",
                   QS_C(TTimeRef::timeref_to_ms_3(fileLocation)),
                   QS_C(TTimeRef::timeref_to_ms_3(slotFileLocation)),
                   QS_C(TTimeRef::timeref_to_ms_3(lastAvailableSlotFileLocation)));

            m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::SyncStatus::OUT_OF_SYNC);
            read = 0;
            m_freeBufferSlotsQueue->try_enqueue(slot); // always put the dequeued slot on the free slots queue so we don't lose slots
            break;
        }

        printf("ReadSource::rb_read: Skipping slot %d, location %s\n",
               slot->get_slot_number(), QS_C(TTimeRef::timeref_to_ms_3(slotFileLocation)));

        m_freeBufferSlotsQueue->try_enqueue(slot); // always put the dequeued slot on the free slots queue so we don't lose slots
    }

    // auto totalTime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    // printf("ReadSource::rb_read: took nanosecs: %ld\n", totalTime);

    return read;
}

TQueueBufferSlot* TBufferedAudioStreamReader::dequeue_from_rt_queue(TProcessCallBackData &processData)
{
    Q_ASSERT(m_bufferstatus.get_sync_status() != TBufferedAudioStreamStatus::QUEUE_SEEKING_TO_NEW_LOCATION);
    Q_ASSERT(m_bufferstatus.get_sync_status() != TBufferedAudioStreamStatus::QUEUE_ABOUT_TO_BE_DELETED);
    Q_ASSERT(is_active()); // A non active read source won't process it's buffer queues so we can't ever enter this function in this state

    TQueueBufferSlot* slot = nullptr;

    if (processData.get_is_real_time()) {
        if (!m_rtBufferSlotsQueue->try_dequeue(slot)) {
            // FIXME
            // What about feedback to user that we're missing out on the
            // audio stream?
        }
    } else {
        auto startTime = TTimeRef::get_nanoseconds_since_epoch();
        m_rtBufferSlotsQueue->wait_dequeue(slot);
        processData.add_ringbuffer_read_wait_time(TTimeRef::get_nanoseconds_since_epoch() - startTime);
    }

    return slot;
}


TBufferedAudioStreamStatus& TBufferedAudioStreamReader::get_buffer_status()
{
    Q_ASSERT(m_channelCount > 0);

    if (!is_active()) {
        m_bufferstatus.set_fill_status(100);
	} else {
        m_bufferstatus.set_fill_status(100 - ((m_freeBufferSlotsQueue->size_approx() * 100) / m_slotcount));
	}

    return m_bufferstatus;
}

void TBufferedAudioStreamReader::set_active(bool active)
{
    m_active.store(active);
}

uint TBufferedAudioStreamReader::get_file_rate() const
{
    if (m_resampleAudioReader) {
        return m_resampleAudioReader->get_file_rate();
    } else {
        PERROR("ReadSource::get_file_rate(), but no audioreader available!!");
    }

    return pm().get_project()->get_rate();
}

QString TBufferedAudioStreamReader::get_error_string() const
{
	switch(m_error) {
		case COULD_NOT_OPEN_FILE: return tr("Could not open file");
		case INVALID_CHANNEL_COUNT: return tr("Invalid channel count");
		case ZERO_CHANNELS: return tr("File has zero channels");
		case FILE_DOES_NOT_EXIST: return tr("The file does not exist!");
	}
	return tr("No ReadSource error set");
}

