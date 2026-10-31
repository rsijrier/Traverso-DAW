/*
Copyright (C) 2007-2026 Remon Sijrier

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

#include "TAudioFileMerger.h"
#include "TAudioDevice.h"
#include <QFile>
#include <QMutexLocker>

#include "TExportSpecification.h"
#include "TAudioBuffer.h"
#include "TBufferedAudioStreamReader.h"
#include "TBufferedAudioStreamWriter.h"
#include "TQueueBufferSlot.h"
#include "defines.h"
#include "Debugger.h"

#include <tuple>

TAudioFileMerger::TAudioFileMerger()
{
    m_stopMerging = false;
    moveToThread(this);
    start();
    connect(this, &TAudioFileMerger::dequeueTask, this, &TAudioFileMerger::dequeue_tasks, Qt::QueuedConnection);
}

void TAudioFileMerger::enqueue_task(TBufferedAudioStreamReader * source0, TBufferedAudioStreamReader * source1, const QString& dir, const QString & outfilename)
{
    MergeTask task;
    task.readsource0 = source0;
    task.readsource1 = source1;
    task.outFileName = outfilename;
    task.dir = dir;

    m_mutex.lock();
    m_tasks.enqueue(task);
    m_mutex.unlock();

    emit dequeueTask();
}

void TAudioFileMerger::dequeue_tasks()
{
    m_mutex.lock();
    if (m_tasks.size()) {
        MergeTask task = m_tasks.dequeue();
        m_mutex.unlock();
        process_task(task);
        return;
    }
    m_mutex.unlock();
}

void TAudioFileMerger::process_task(MergeTask task)
{
    QString name = task.readsource0->get_name();
    int length = name.length();

    emit taskStarted(name.left(length-28));

    TAudioBuffer decodebuffer0;
    TAudioBuffer decodebuffer1;
    TAudioBuffer fileIOBuffer;

    TExportSpecification spec;
    spec.set_export_start_location(TTimeRef());
    spec.set_export_end_location(task.readsource0->get_length());

    spec.set_export_dir(task.dir);
    spec.set_file_format(TraversoDAW::FileFormat::WAV);
    spec.set_channel_count(2); // Hardcoded to stereo merge execution target
    spec.set_sample_rate(task.readsource0->get_sample_rate());
    spec.set_export_file_name(task.outFileName);

    TBufferedAudioStreamWriter writesource(spec.get_export_dir(), spec.get_export_file_name());
    // The frames come at the device's rate (T5)
    if (writesource.prepare_file(&spec, audiodevice().get_sample_rate()) == -1) {
        return;
    }

    writesource.set_process_peaks(true);

    do {
        if (m_stopMerging) {
            PMESG("AudioFileMerger::process_task: Stop Merging was requested, breaking out of process loop");
            break;
        }

        nframes_t diff = spec.get_remaining_export_frames();
        nframes_t this_nframes = std::min(diff, spec.get_block_size());
        nframes_t nframes = this_nframes;

        // Read errors are not handled yet (task T10): zeros in the merge
        std::ignore = task.readsource0->file_read(decodebuffer0, spec.get_export_location(), nframes);
        std::ignore = task.readsource1->file_read(decodebuffer1, spec.get_export_location(), nframes);

        TQueueBufferSlot virtualSlot(0, 2, nframes);
        virtualSlot.update_location_and_offset(TTimeRef(), TTimeRef());
        virtualSlot.write_channel(0, decodebuffer0.get_channel_buffer(0), 0, nframes);
        virtualSlot.write_channel(1, decodebuffer1.get_channel_buffer(0), 0, nframes);

        writesource.rb_file_write(&virtualSlot, fileIOBuffer);

        spec.add_exported_range(TTimeRef(nframes, task.readsource0->get_sample_rate()));

    } while (spec.get_remaining_export_frames() > 0);

    // Always: the audio file complete (its writer closed) and its peaks, also
    // when the merge was stopped
    writesource.finish_file();

    if (m_stopMerging) {
        exit(0);
        wait(1000);
        m_tasks.clear();
        emit processingStopped();
        return;
    }

    emit taskFinished(name.left(length-28));
}

void TAudioFileMerger::stop_merging()
{
    m_stopMerging = true;
}

