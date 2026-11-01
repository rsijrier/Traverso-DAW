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
#include "TAudioReader.h"

#include "TProjectManager.h"
#include "TProject.h"
#include "AudioBus.h"
#include "TAudioBuffer.h"
#include "TQueueBufferSlot.h"
#include "Utils.h"
#include "Debugger.h"
#include "TLog.h"

#include <QFile>
#include <QThread>

#include <algorithm>
#include <tuple>

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

    const std::expected<std::unique_ptr<TAudioReader>, TAudioFileError> reader = TAudioReader::open(m_fileName);
    m_channelCount = reader ? (*reader)->channel_count() : 0;

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
    m_errorCode = 0;

    m_aboutOneToFourSecondsTime = TTimeRef::UNIVERSAL_SAMPLE_RATE * (TraversoDAW::Utils::randomNumberBetween(0, 3) + 1.0);
}

TBufferedAudioStreamReader::~TBufferedAudioStreamReader()
{
    PENTERDES;
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
        return (m_errorCode = INVALID_CHANNEL_COUNT);
    }

    // A reader exists only for a file it reads (channels, rate and frames
    // there), or says why there is none. A missing file is asked after the
    // open, not before (a race): NotFound stays FILE_DOES_NOT_EXIST, so what
    // the GUI does with a missing file (relink it) carries over
    std::expected<std::unique_ptr<TAudioReader>, TAudioFileError> reader = TAudioReader::open(m_fileName);
    if (!reader) {
        TLOG_W("audiofile", "{}: cannot read it ({})", m_fileName.toStdString(), reader.error().detail.toStdString());
        return (m_errorCode = reader.error().code == TAudioFileError::Code::NotFound ? FILE_DOES_NOT_EXIST : COULD_NOT_OPEN_FILE);
    }
    m_audioReader = std::move(*reader);

    // At the file's own rate: no resampler, so no choice of one; the IO
    // scheduler hands the configured one on with the device's rate
    set_output_rate_and_resampler(m_audioReader->file_rate(), Resampler{});

    m_channelCount = m_audioReader->channel_count();
    m_rate = m_audioReader->file_rate();
    m_length = m_audioReader->length();

    return 1;
}


void TBufferedAudioStreamReader::set_output_rate_and_resampler(int outputRate, const Resampler& resampler)
{
    Q_ASSERT(outputRate > 0);
    Q_ASSERT_X(m_audioReader, "ReadSource::set_output_rate_and_resampler", "No Resample Audio Reader");

    // Without resampling the file is read at its own rate (and plays at
    // another pitch when the device's rate is not the file's). The choice
    // comes with the resampler, from the owner's thread: this runs in the
    // IO scheduler's threads too, where TConfig must not be read
    const uint readRate = resampler.resample ? uint(outputRate) : m_audioReader->file_rate();
    // A resampler that does not start leaves the reader as it was (at its
    // old rate), and so the stream, which times its slots by that rate: said
    // here, the stream plays on (its error state: task T10)
    if (TAudioFileStatus set = m_audioReader->set_output_rate_and_resampler(readRate, resampler.choice); !set) {
        TLOG_W("audiofile", "{}: no output rate of {} Hz ({})", m_fileName.toStdString(), readRate, set.error().detail.toStdString());
        return;
    }

    m_outputRate = outputRate;
}

void TBufferedAudioStreamReader::set_source_start_location(const TTimeRef &sourceStartLocation)
{
    // printf("ReadSource::set_source_start_location: %s\n", QS_C(TTimeRef::timeref_to_ms_3(sourceStartLocation)));
    m_sourceStartLocation = sourceStartLocation;
}

TAudioFileResult TBufferedAudioStreamReader::file_read(TAudioBuffer& buffer, const TTimeRef& fileLocation, nframes_t count) const
{
    Q_ASSERT(m_audioReader);
    return m_audioReader->read(buffer, fileLocation, count);
}


TAudioFileResult TBufferedAudioStreamReader::file_read_frames(TAudioBuffer& buffer, nframes_t firstFrame, nframes_t count) const
{
    Q_ASSERT(m_audioReader);
    return m_audioReader->read_file_frames(buffer, firstFrame, count);
}


const TAudioIOStats& TBufferedAudioStreamReader::io_stats() const
{
    Q_ASSERT(m_audioReader);
    return m_audioReader->io_stats();
}

TBufferedAudioStreamReader * TBufferedAudioStreamReader::deep_copy( )
{
    PENTER;

    QDomDocument doc("ReadSource");
    QDomNode rsnode = get_state(doc);
    TBufferedAudioStreamReader* source = new TBufferedAudioStreamReader(rsnode);
    return source;
}

// The frames of the file at its own rate, at any output rate
nframes_t TBufferedAudioStreamReader::get_nframes( ) const
{
    if (!m_audioReader) {
        return 0;
    }
    return m_audioReader->file_frame_count();
}

int TBufferedAudioStreamReader::set_file(const QString & filename)
{
    PENTER;

    m_errorCode = 0;

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


void TBufferedAudioStreamReader::rb_seek_to_transport_location(TAudioBuffer &fileDecodeBuffer, const TTimeRef& transportLocation)
{
    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::QUEUE_SEEKING_TO_NEW_LOCATION);

    // If the transport location lies (much) in front of our start location
    // or after our end location no need to fill the buffers
    if (!transport_in_window(transportLocation)) {
        m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::SyncStatus::OUT_OF_WINDOW);
        return;
    }

    // A seek starts from nothing, also to where the reader happens to be: the
    // decoder and the resampler keep nothing of the position before
    m_audioReader->forget_position();

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
    // The slot's size, not the device's: this runs on an IO thread, and the
    // ring was made for this size
    nframes_t bufferSize = m_bufferSlotFrames;
    // if modules != 0 then the seek transport location is not a multiple of transport location + x * buffer slot duration
    if (modulus != TTimeRef()) {
        // The file location of the slot's start: before the source start,
        // before the file's start for a clip that plays it from its start
        fileLocation -= modulus;
        // keep track of the seek transport location as well, probably not usefull to keep track of 2 locations in slot buffers?
        seekTransportLocation -= modulus;

        // The clip starts modulus into this slot: its first frame goes to
        // slot position offset, the frame nearest to modulus (the same
        // rounding of the same time as TAudioClip::process())
        nframes_t offset = TTimeRef::to_nframes(TTimeRef::frame_nearest(modulus, m_outputRate));
        // printf("file location after adjustment %s, offset nframes %d\n", QS_C(TTimeRef::timeref_to_ms_3(fileLocation)), offset);

        // A clip whose first frame rounds into the next slot has none in this
        // one: TAudioClip::process() rounds the same time the same way and
        // does not ask for it, the stream starts at the next slot
        if (offset < bufferSize) {
            // and only read in the amount of frames needed for this buffer slot
            nframes_t toRead = bufferSize - offset;

            // Read at the time of slot position offset, as every refill reads at
            // the time of its slot: all reads of the stream lie whole frames
            // apart, so the reader's one rounding (TTimeRef::frame_nearest())
            // joins them, no frame lost or doubled (test_resample_reader). Not at
            // the source start: that is off the slots' frames by offset - modulus,
            // and the reads would join for some fractions only. The time is at
            // most half a frame before the source start, so its frame is never
            // before the file. The reader makes room in the buffer and gives zeros
            // past the end of the file.
            auto [read, error] = file_read(fileDecodeBuffer, fileLocation + TTimeRef(offset, m_outputRate), toRead);
            if (error) {
                // What the stream does on a read error is task T10
                TLOG_W("audiofile", "{}: read {} of {} frames ({})", m_fileName.toStdString(), read, toRead, error->detail.toStdString());
            }
            if (stale()) {
                return;
            }

            if (!m_freeBufferSlotsQueue->try_dequeue(slot)) {
                TLog::write_printf(TLogLevel::Warning, TLOG_FILE_CATEGORY, "ReadSource::rb_seek_to_transport_location:: try dequeue failed");
                m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::FILL_RTBUFFER_DEQUEUE_FAILURE);
                return;
            }

            // and now write it into the buffer using the offset
            slot->update_location_and_offset(seekTransportLocation, fileLocation, offset);
            for (uint chan=0; chan<m_channelCount; ++chan) {
                slot->write_channel(chan, fileDecodeBuffer.get_channel_buffer(chan), 0, toRead);
            }

            if (!m_rtBufferSlotsQueue->try_enqueue(slot)) {
                TLog::write_printf(TLogLevel::Warning, TLOG_FILE_CATEGORY, "ReadSource::fill_realtime_buffers: try enqueue failed");
                m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::FILL_RTBUFFER_ENQUEUE_FAILURE);
                return;
            }
        }

        fileLocation = m_sourceStartLocation + m_bufferSlotDuration - modulus;
        seekTransportLocation += m_bufferSlotDuration;
    }

    m_lastQueuedRTBufferSlot->set_file_location(fileLocation);
    m_lastQueuedRTBufferSlot->set_transport_location(seekTransportLocation);
    m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::QUEUE_SEEKED_TO_NEW_LOCATION);

    process_realtime_buffers(fileDecodeBuffer);
}

bool TBufferedAudioStreamReader::transport_in_window(const TTimeRef& transportLocation) const
{
    return !((transportLocation + m_aboutOneToFourSecondsTime) < m_streamStart || transportLocation > m_streamEnd);
}

TIOStream::State TBufferedAudioStreamReader::disk_state(const TTimeRef& transportLocation)
{
    TBufferedAudioStreamStatus& status = get_buffer_status();
    if (status.get_sync_status() == TBufferedAudioStreamStatus::OUT_OF_WINDOW) {
        return TIOStream::State{transport_in_window(transportLocation), false, status.get_fill_status()};
    }
    return TIOStream::State{status.needs_disk_work(), true, status.get_fill_status()};
}

void TBufferedAudioStreamReader::process_realtime_buffers(TAudioBuffer& fileDecodeBuffer)
{
    Q_ASSERT(m_lastQueuedRTBufferSlot);
    Q_ASSERT(m_channelCount > 0);

    // printf("ReadSource::fill_realtime_buffers\n");

    auto freeSlots = m_freeBufferSlotsQueue->size_approx();
    if (freeSlots == 0) {
        TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "Free Buffer Slots Queue is empty, why was I called?");
        return;
    }

    TTimeRef slotFileLocation = m_lastQueuedRTBufferSlot->get_file_location();
    TTimeRef transportLocation = m_lastQueuedRTBufferSlot->get_transport_location();
    // The slot size the ring was made for, as in the seek
    const nframes_t bufferSize = m_bufferSlotFrames;

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
    // The slots below take all totalReadSize frames, also past the end of
    // the file: file_read() fills every one of them, zeros where the file
    // ends (the contract of TAudioReader::read()), so no stale audio
    // reaches the ring
    // The error of the read is dropped here on purpose: what the stream
    // does on a read error is task T10
    std::ignore = file_read(fileDecodeBuffer, slotFileLocation, totalReadSize);
    nframes_t offset = 0;

    while (slotsToFill)
    {
        // Slots that do get in go with the seek: it empties the queue
        if (stale()) {
            return;
        }

        if (!m_freeBufferSlotsQueue->try_dequeue(slot)) {
            PERROR("ReadSource::fill_realtime_buffers: try dequeue failed");
            m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::FILL_RTBUFFER_DEQUEUE_FAILURE);
            return;
        }

        slot->update_location_and_offset(transportLocation, slotFileLocation);
        for (uint chan=0; chan<m_channelCount; ++chan) {
            slot->write_channel(chan, fileDecodeBuffer.get_channel_buffer(chan), offset, bufferSize);
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


nframes_t TBufferedAudioStreamReader::ringbuffer_read(TProcessContext &context, const TTimeRef &fileLocation) T_REALTIME_SAFE
{
    if (m_bufferstatus.out_of_sync()) {
        if (context.get_is_real_time()) {
            // Numbers only: a time as text is a QString, built before the
            // log call, also when debug is off
            TLOG_RT_D("diskio", "ring read: out of sync, skipping file location {} (universal frames), status {}",
                      fileLocation.universal_frame(), m_bufferstatus.get_sync_status());
            return 0;
        } else {
            // During freewheeling the status of the buffers can get out of sync
            // Reason: Buffers get only filled when the transport location get's close to the start
            // of an AudioClip, about 3-4 seconds the buffers get filled. If the DiskIO thread
            // is slow or overloaded compared to the audio thread, the latter one can get in front of the disk thread
            // It's perfectly valid and in fact mandatory to start waiting here to let the disk thread
            // fill the buffers and then continue freewheeling
            TLOG_RT_D("diskio", "ring read: freewheeling, status {}, waiting for the buffers to get back in sync", m_bufferstatus.get_sync_status());
            uint counter = 0;
            while(m_bufferstatus.out_of_sync() && (counter < 100000)) {
                // Freewheeling has no deadline: the wait for the disk thread
                // is by design (doc/design/realtime-audio-path.md §5)
                T_BLOCKING_BY_DESIGN(QThread::sleep(std::chrono::nanoseconds(10000));)
                counter++;
            }
            if (m_bufferstatus.out_of_sync()) {
                TLOG_RT_D("diskio", "ring read: freewheeling, the buffers still out of sync after a second, giving up");
                return 0;
            } else {
                TLOG_RT_D("diskio", "ring read: the buffers back in sync after {} waits of 10 microseconds, continuing", counter);
            }
        }
    }

    TQueueBufferSlot* slot = nullptr;

    // auto startTime = TTimeRef::get_nanoseconds_since_epoch();
    nframes_t read = 0;
    nframes_t nframes = context.get_nframes_to_process();
    AudioBus* bus = context.get_ringbuffer_read_bus();


    // Real-time safe by contract, as the moodycamel calls below: no
    // allocation, no wait (try_enqueue() wakes the disk thread's semaphore
    // when it sleeps)
    size_t availableSlots = 0;
    T_REALTIME_SAFE_BY_CONTRACT(availableSlots = m_rtBufferSlotsQueue->size_approx();)

    while ((slot = dequeue_from_rt_queue(context)))
    {
        Q_ASSERT(m_bufferstatus.get_sync_status() != TBufferedAudioStreamStatus::QUEUE_SEEKING_TO_NEW_LOCATION);
        Q_ASSERT(m_bufferstatus.get_sync_status() != TBufferedAudioStreamStatus::QUEUE_ABOUT_TO_BE_DELETED);
        Q_ASSERT(slot);

        TTimeRef slotFileLocation = slot->get_file_location();

        Q_ASSERT(slotFileLocation != TTimeRef::INVALID);

        if (slotFileLocation == fileLocation)
        {
            // The channels the bus has (TAudioClip::process())
            const uint channels = std::min(m_channelCount, bus->get_channel_count());
            for (uint chan=0; chan < channels; ++chan) {
                slot->read_buffer(bus->get_buffer(chan), chan, nframes);
            }

            read = slot->get_read_nframes();

            T_REALTIME_SAFE_BY_CONTRACT(m_freeBufferSlotsQueue->try_enqueue(slot);) // always put the dequeued slot on the free slots queue so we don't lose slots
            break;
        }

        TTimeRef lastAvailableSlotFileLocation = slotFileLocation + (availableSlots * m_bufferSlotDuration);

        // check if this slot or any available is a candidate slot, if not, no need to process the
        // whole queue, instead start a resync
        if ((fileLocation < slotFileLocation) || (fileLocation > lastAvailableSlotFileLocation)) {
            TLOG_RT_D("diskio", "ring read: file location {} not in the queue's range {} - {} (universal frames)",
                      fileLocation.universal_frame(), slotFileLocation.universal_frame(), lastAvailableSlotFileLocation.universal_frame());

            m_bufferstatus.set_sync_status(TBufferedAudioStreamStatus::SyncStatus::OUT_OF_SYNC);
            read = 0;
            T_REALTIME_SAFE_BY_CONTRACT(m_freeBufferSlotsQueue->try_enqueue(slot);) // always put the dequeued slot on the free slots queue so we don't lose slots
            break;
        }

        TLOG_RT_D("diskio", "ring read: skipping slot {}, location {} (universal frames)", slot->get_slot_number(), slotFileLocation.universal_frame());

        T_REALTIME_SAFE_BY_CONTRACT(m_freeBufferSlotsQueue->try_enqueue(slot);) // always put the dequeued slot on the free slots queue so we don't lose slots
    }

    // auto totalTime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    // printf("ReadSource::rb_read: took nanosecs: %ld\n", totalTime);

    return read;
}

TQueueBufferSlot* TBufferedAudioStreamReader::dequeue_from_rt_queue(TProcessContext &context) T_REALTIME_SAFE
{
    Q_ASSERT(m_bufferstatus.get_sync_status() != TBufferedAudioStreamStatus::QUEUE_SEEKING_TO_NEW_LOCATION);
    Q_ASSERT(m_bufferstatus.get_sync_status() != TBufferedAudioStreamStatus::QUEUE_ABOUT_TO_BE_DELETED);
    Q_ASSERT(is_active()); // A non active read source won't process it's buffer queues so we can't ever enter this function in this state

    TQueueBufferSlot* slot = nullptr;

    if (context.get_is_real_time()) {
        bool dequeued = false;
        // Real-time safe by contract: moodycamel's try_dequeue does not wait
        T_REALTIME_SAFE_BY_CONTRACT(dequeued = m_rtBufferSlotsQueue->try_dequeue(slot);)
        if (!dequeued) {
            // FIXME
            // What about feedback to user that we're missing out on the
            // audio stream?
        }
    } else {
        const trav_time_t startTime = TTimeRef::get_nanoseconds_since_epoch();
        // Freewheeling has no deadline: the wait for the disk thread is by
        // design (doc/design/realtime-audio-path.md §5)
        T_BLOCKING_BY_DESIGN(m_rtBufferSlotsQueue->wait_dequeue(slot);)
        const trav_time_t endTime = TTimeRef::get_nanoseconds_since_epoch();
        context.add_ringbuffer_read_wait_time(endTime - startTime);
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
    if (m_audioReader) {
        return m_audioReader->file_rate();
    } else {
        PERROR("ReadSource::get_file_rate(), but no audioreader available!!");
    }

    return pm().get_project()->get_rate();
}

QString TBufferedAudioStreamReader::get_error_string() const
{
	switch(m_errorCode) {
		case COULD_NOT_OPEN_FILE: return tr("Could not open file");
		case INVALID_CHANNEL_COUNT: return tr("Invalid channel count");
		case ZERO_CHANNELS: return tr("File has zero channels");
		case FILE_DOES_NOT_EXIST: return tr("The file does not exist!");
	}
	return tr("No ReadSource error set");
}

