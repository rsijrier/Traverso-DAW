/*
Copyright (C) 2005-2026 Remon Sijrier

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

#include "TBufferedAudioStreamStatus.h"
#include "TAudioBuffer.h"
#include "TIOStream.h"
#include "TTimeRef.h"
#include "TTypes.h"

#include "cameron/readerwritercircularbuffer.h"

#include <QObject>

class TQueueBufferSlot;

/// The base class for AudioSources like ReadSource and WriteSource: the
/// streams TIOScheduler reads and writes
struct TAudioIOStats;

class TBufferedAudioStream : public QObject, public TIOStream
{
    Q_OBJECT

public :
    TBufferedAudioStream();
    TBufferedAudioStream(QString  dir, const QString& name);
    virtual ~TBufferedAudioStream();

    void set_name(const QString& name);
    void set_dir(const QString& name);
    void set_original_bit_depth(uint bitDepth);
    void set_created_by_sheet(qint64 id);
    QString get_filename() const;
    QString get_dir() const;
    QString get_name() const;
    QString get_short_name() const;
    qint64 get_id() const {return m_id;}
    qint64 get_orig_sheet_id() const {return m_origSheetId;}
    uint get_sample_rate() const;
    uint get_channel_count() const T_REALTIME_SAFE override {return m_channelCount;}
    uint get_bit_depth() const;

    virtual TBufferedAudioStreamStatus& get_buffer_status() = 0;
    uint get_output_rate() const {return m_outputRate;}


protected:
    // A seek came after this step's generation: what it read is not wanted
    bool stale() const { return generation() != m_stepGeneration; }

    // The generation of the step that runs
    quint64 m_stepGeneration{0};

    TBufferedAudioStreamStatus		m_bufferstatus;

    moodycamel::BlockingReaderWriterCircularBuffer<TQueueBufferSlot*> *m_rtBufferSlotsQueue;
    moodycamel::BlockingReaderWriterCircularBuffer<TQueueBufferSlot*> *m_freeBufferSlotsQueue;
    TQueueBufferSlot*    m_lastQueuedRTBufferSlot;

    TTimeRef            m_bufferSlotDuration;
    // A slot's frames at the output rate (prepare_ring_buffers()); the IO
    // steps use it, never the device's current buffer size
    nframes_t           m_bufferSlotFrames{0};
    uint                m_outputRate;
    size_t              m_slotcount;

    uint		m_channelCount;
    qint64		m_origSheetId{};
    QString 	m_dir;
    qint64		m_id{};
    QString 	m_name;
    QString		m_shortName;
    uint		m_origBitDepth{};
    QString		m_fileName;
    // FIMXE : use output rate instead ?
    uint 		m_rate{};
    int         m_wasRecording;

public:
    // TIOStream: the scheduler's step, and the ring buffers it makes and frees
    int step(const StepInfo& info) override;
    void prepare_ring_buffers(nframes_t bufferSize) override;
    void retire() override;

private:
    void delete_queue_buffers();
    // What the stream's decoder read and decoded since it opened (a reader
    // with a file); none: nothing to report (a writer, a reader without file)
    virtual const TAudioIOStats* decoder_stats() const { return nullptr; }
    // What a step does, in the reader or the writer: read or write all there
    // is room for, or seek
    virtual void process_realtime_buffers(TAudioBuffer &fileDecodeBuffer) = 0;
    virtual void rb_seek_to_transport_location(TAudioBuffer& fileDecodeBuffer, const TTimeRef &transportLocation) = 0;
};

