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

#include "DiskIO.h"

#include "AbstractAudioReader.h"
#include "AudioDevice.h"

#include "AudioSource.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"
#include <samplerate.h>

#if defined (Q_OS_UNIX)

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>

#if defined(__i386__)
# define __NR_ioprio_set	289
# define __NR_ioprio_get	290
# define IOPRIO_SUPPORT		1
#elif defined(__ppc__) || defined(__powerpc__) || defined(__PPC__)
# define __NR_ioprio_set	273
# define __NR_ioprio_get	274
# define IOPRIO_SUPPORT		1
#elif defined(__x86_64__)
# define __NR_ioprio_set	251
# define __NR_ioprio_get	252
# define IOPRIO_SUPPORT		1
#elif defined(__ia64__)
# define __NR_ioprio_set	1274
# define __NR_ioprio_get	1275
# define IOPRIO_SUPPORT		1
#else
# define IOPRIO_SUPPORT		0
#endif

enum {
    IOPRIO_CLASS_NONE,
    IOPRIO_CLASS_RT,
    IOPRIO_CLASS_BE,
    IOPRIO_CLASS_IDLE,
};

enum {
    IOPRIO_WHO_PROCESS = 1,
    IOPRIO_WHO_PGRP,
    IOPRIO_WHO_USER,
};

const char *to_prio[] = { "none", "realtime", "best-effort", "idle", };
#define IOPRIO_CLASS_SHIFT	13
#define IOPRIO_PRIO_MASK	0xff

#endif // endif Q_OS_UNIX

/** \class DiskIO
 *	\brief handles all the read's and write's of AudioSources in it's private thread.
 *
 *	Each Sheet class has it's own DiskIO instance.
 * 	The DiskIO manages all the AudioSources related to a Sheet, and makes sure the RingBuffers
 * 	from the AudioSources are processed in time. (It at least tries very hard)
 */


void DiskIO::run()
{
#if defined (Q_OS_UNIX)

    // struct sched_param param;
    // param.sched_priority = 40;
    // if (pthread_setschedparam (pthread_self(), SCHED_FIFO, &param) != 0) {}

    if (IOPRIO_SUPPORT) {
        // When using the cfq scheduler we are able to set the priority of the io for what it's worth though :-)
        int ioprio = 0, ioprio_class = IOPRIO_CLASS_RT;
        int value = syscall(__NR_ioprio_set, IOPRIO_WHO_PROCESS, getpid(), ioprio | ioprio_class << IOPRIO_CLASS_SHIFT);

        if (value == -1) {
            ioprio_class = IOPRIO_CLASS_BE;
            value = syscall(__NR_ioprio_set, IOPRIO_WHO_PROCESS, getpid(), ioprio | ioprio_class << IOPRIO_CLASS_SHIFT);
        }

        if (value == 0) {
            ioprio = syscall (__NR_ioprio_get, IOPRIO_WHO_PROCESS, getpid());
            ioprio_class = ioprio >> IOPRIO_CLASS_SHIFT;
            ioprio = ioprio & IOPRIO_PRIO_MASK;
            printf("DiskIOThread: Using prioritized disk I/O using %s prio %d (Only effective with the cfq scheduler)\n", to_prio[ioprio_class], ioprio);
        }
    }
#endif
    exec();
}



DiskIO::DiskIO()
{
    m_waitForSeek.store(false);
    m_outputSampleRate = 0;
    m_sampleRateChanged = false;
    m_resampleQualityChanged = false;
    m_resampleQuality = SRC_SINC_FASTEST;
    m_bufferFillStatus = 0;
    m_cpuTime = new RingBufferNPT<trav_time_t>(1024);
    m_lastCpuReadTime = TTimeRef::get_nanoseconds_since_epoch();

    // TODO This is a LARGE buffer, any ideas how to make it smaller ??
    // FIXME: this buffer is never resized and an ugly hack so fix it!
    framebuffer = new audio_sample_t[audiodevice().get_sample_rate() * writebuffertime];

    m_fileDecodeBuffer = new DecodeBuffer;
    m_resampleDecodeBuffer = new DecodeBuffer;

    // Run in our own event loop so every slot call get's processed there
    moveToThread(this);
    start(QThread::HighPriority);

    connect(&audiodevice(), SIGNAL(finishedOneProcessCycle()), this, SLOT(do_work()), Qt::QueuedConnection);
}


DiskIO::~DiskIO()
{
    PENTERDES;
    stop_disk_thread();
    delete framebuffer;
    delete m_fileDecodeBuffer;
    delete m_resampleDecodeBuffer;
    delete m_cpuTime;

}

/**
* 	Seek's all the ReadSources readbuffers to the new position.
*	Call prepare_seek() first, to interupt do_work() if it was running.
* 
*  N.B. this function resets the ReadSource buffers assuming it is the only thread
*  accessing the buffers. If the audio thread is accessing the buffers at this point
*  the integrity of the buffers cannot be garuanteed!
*/
void DiskIO::seek()
{
    PENTER;

    Q_ASSERT_X(this->thread() == QThread::currentThread(), "DiskIO::seek", "NOT running in DiskIO thread");

    auto startTime = TTimeRef::get_nanoseconds_since_epoch();

    // A seek event happens for 2 reasons, for transport control and after an audiodevice reconfiguration
    // in the latter case we need to reset rate and buffer sizes.
    if (m_sampleRateChanged) {
        for (auto source : m_audioSources) {
            source->set_output_rate_and_convertor_type(m_outputSampleRate, m_resampleQuality);
            source->prepare_rt_buffers(audiodevice().get_buffer_size());
        }
        m_sampleRateChanged = false;
    }

    for(auto source : m_audioSources) {
        source->rb_seek_to_transport_location(m_seekTransportLocation);
    }

    auto totalTime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    m_cpuTime->write(&totalTime, 1);

    m_waitForSeek.store(false);
    emit seekFinished();
}


// Internal function
// This function is called everytime the audio thread has finished one processing cycle
void DiskIO::do_work( )
{
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "DiskIO::do_work", "NOT running in DiskIO thread");

    auto startTime = TTimeRef::get_nanoseconds_since_epoch();

    if (m_resampleQualityChanged) {
        for (auto source : m_audioSources) {
            source->set_output_rate_and_convertor_type(m_outputSampleRate, m_resampleQuality);
        }
        m_resampleQualityChanged = false;
    }


    for (auto source : m_audioSources)
    {
        if (m_waitForSeek.load()) {
            printf("DiskIO::do_work: waiting for seek\n");
            return;
        }

        BufferStatus* status = source->get_buffer_status();

        if (status->fillStatus < 80 || status->out_of_sync()) {

            if (status->out_of_sync()) {
                source->rb_seek_to_transport_location(m_transportLocation);
            }
            else {
                source->process_realtime_buffers();
            }

            if ((status->fillStatus < m_bufferFillStatus.load()) && !status->out_of_sync()) {
                m_bufferFillStatus.store(status->fillStatus);
            }
        }
    }

    auto totalTime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    m_cpuTime->write(&totalTime, 1);
}


void DiskIO::add_audio_source(AudioSource* source)
{
    PENTER2;

    Q_ASSERT_X(this->thread() == QThread::currentThread(), "DiskIO::addd_audio_source", "Must be called via queued slot connection, not directly by function");
    Q_ASSERT(source->get_channel_count() > 0);

    source->set_output_rate_and_convertor_type(m_outputSampleRate, m_resampleQuality);
    source->set_decode_buffers(m_fileDecodeBuffer, m_resampleDecodeBuffer);

    source->prepare_rt_buffers(audiodevice().get_buffer_size());

    // only for WriteSource change to decodebuffers instead
    source->set_diskio_frame_buffer(framebuffer);

    m_audioSources.append(source);
}

void DiskIO::remove_and_delete_audio_source(AudioSource *source)
{
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "DiskIO::remove_audio_source", "Must be called via queued slot connection, not directly by function");

    m_audioSources.removeAll(source);
    // FIXME
    // Review the deletion of AudioSources and non-active AudioSources that should only
    // be removed from DiskIO but not deleted. Currently this function is only called
    // for removing WriteSource source since they only live while recording
    source->delete_rt_buffers();
    delete source;
}

/**
 *
 * @return Returns the CPU time consumed by the DiskIO thread
 */
bool DiskIO::get_cpu_time(float &time)
{
    trav_time_t currentTime = TTimeRef::get_nanoseconds_since_epoch();
    float totaltime = 0;
    trav_time_t value = 0;
    int read = m_cpuTime->read_space();
    if (read == 0) {
        return false;
    }

    while (read != 0) {
        read = m_cpuTime->read(&value, 1);
        totaltime += value;
    }

    time = ( (totaltime  / (currentTime - m_lastCpuReadTime) ) * 100 );

    m_lastCpuReadTime = currentTime;

    return true;
}


/**
 * 	Get the status of the writebuffers.
 *
 * @return The status in procentual amount of the smallest remaining space in the writebuffers
 *		that could be used to write 'recording' data too.
 */
int DiskIO::get_buffers_fill_status( )
{
    int status = m_bufferFillStatus.load();
    m_bufferFillStatus.store(100);

    return status;
}

void DiskIO::stop_disk_thread( )
{
    PENTER;
    if (!isRunning()) {
        return;
    }

    // Stop any processing in do_work()
    m_waitForSeek.store(true);

    // Exit the diskthreads event loop
    printf("DiskIO::stop_disk_thread: calling m_diskThread->exit(0)\n");
    exit(0);

    // Wait for the Thread to return from it's event loop. 2 seconds should be (more then) enough,
    // if not, terminate this thread and print a warning!
    if ( ! wait(2000) ) {
        qWarning("DiskIO :: Still running after 2 second wait, terminating!");
        terminate();
    }
}

void DiskIO::set_resample_quality(int quality)
{
    m_resampleQuality = quality;
    m_resampleQualityChanged = true;
}

void DiskIO::set_output_sample_rate(uint outputSampleRate)
{
    printf("DiskIO::set_output_sample_rate: new sample rate %d\n", outputSampleRate);
    m_outputSampleRate = outputSampleRate;
    m_sampleRateChanged = true;
}

