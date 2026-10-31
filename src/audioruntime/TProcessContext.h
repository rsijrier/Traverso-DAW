/*
Copyright (C) 2024 Remon Sijrier

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

#include "TTimeRef.h"
#include "TTypes.h"

class AudioBus;

class TProcessContext {

public:
    TProcessContext() {
        m_ringBufferReadBus = nullptr;
        m_ringBufferWriteBus = nullptr;
        m_nFramesToProcess = 0;
        m_startLocation = TTimeRef();
        m_endLocation = TTimeRef();
        m_ringBufferReadWaitTime = 0;
        m_realTime = true;
        m_sampleRate = 0;
    }

    void set_ringbuffer_read_bus(AudioBus* bus) {
        m_ringBufferReadBus = bus;
    }
    void set_ringbuffer_write_bus(AudioBus* bus) {
        m_ringBufferWriteBus = bus;
    }
    void set_start_location(const TTimeRef& location) {
        m_startLocation = location;
        m_endLocation = m_startLocation + TTimeRef(m_nFramesToProcess, m_sampleRate);
    }

    AudioBus* get_ringbuffer_read_bus() const {
        Q_ASSERT(m_ringBufferReadBus);
        return m_ringBufferReadBus;
    }
    AudioBus* get_ringbuffer_write_bus() {
        Q_ASSERT(m_ringBufferWriteBus);
        AudioBus* bus = m_ringBufferWriteBus;
        m_ringBufferWriteBus = nullptr;
        return bus;
    }
    nframes_t get_nframes_to_process() const {return m_nFramesToProcess;}
    TTimeRef get_start_location() const {return m_startLocation;}
    TTimeRef get_end_location() const {return m_endLocation;}
    bool get_is_real_time() const {return m_realTime;}
    uint get_sample_rate() const {
        Q_ASSERT(m_sampleRate != 0);
        return m_sampleRate;
    }
    // When this cycle started, on the clock of TClock::now_nanoseconds()
    uint64_t get_cycle_start_nanoseconds() const {return m_cycleStartNanoseconds;}

    void add_ringbuffer_read_wait_time(trav_time_t time) {
        m_ringBufferReadWaitTime += time;
    }

private:
    AudioBus*   m_ringBufferReadBus;
    AudioBus*   m_ringBufferWriteBus;
    nframes_t   m_nFramesToProcess;
    TTimeRef    m_startLocation;
    TTimeRef    m_endLocation;
    trav_time_t m_ringBufferReadWaitTime;
    bool        m_realTime;
    uint        m_sampleRate;
    uint64_t    m_cycleStartNanoseconds{0};

    void set_nframes_to_process(nframes_t nframes) {
        m_nFramesToProcess = nframes;
    }
    void set_real_time(bool realTime) {
        m_realTime = realTime;
    }
    void set_cycle_start_nanoseconds(uint64_t nanoseconds) {
        m_cycleStartNanoseconds = nanoseconds;
    }
    void set_sample_rate(uint rate) {
        m_sampleRate= rate;
    }
    trav_time_t get_ringbuffers_read_write_wait_time() {
        auto time = m_ringBufferReadWaitTime;
        m_ringBufferReadWaitTime = 0;
        return time;
    }

    friend class TAudioDevice;

};

