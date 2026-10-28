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

#include "TAudioFileCopyConvert.h"
#include "TAudioDevice.h"
#include <QFile>
#include <QMutexLocker>
#include <QFileInfo>

#include "TExportSpecification.h"
#include "TProjectManager.h"
#include "TQueueBufferSlot.h"
#include "TResourcesManager.h"
#include "TBufferedAudioStreamReader.h"
#include "TAudioBuffer.h"
#include "TBufferedAudioStreamWriter.h"
#include "defines.h"

#include <tuple>

TAudioFileCopyConvert::TAudioFileCopyConvert()
{
    m_stopProcessing = false;
    moveToThread(this);
    start();
    connect(this, &TAudioFileCopyConvert::dequeueTask, this, &TAudioFileCopyConvert::dequeue_tasks, Qt::QueuedConnection);
}

void TAudioFileCopyConvert::enqueue_task(TBufferedAudioStreamReader * source,
                                         TExportSpecification* spec,
                                         const QString& dir,
                                         const QString& outfilename,
                                         int tracknumber,
                                         const QString& trackname)
{
    QFileInfo fi(outfilename);

    CopyTask task;
    task.readsource = source;
    task.outFileName = fi.completeBaseName();
    task.extension = fi.suffix();
    task.tracknumber = tracknumber;
    task.trackname = trackname;
    task.dir = dir;
    task.spec = spec;

    m_mutex.lock();
    m_tasks.enqueue(task);
    m_mutex.unlock();

    emit dequeueTask();
}

void TAudioFileCopyConvert::dequeue_tasks()
{
    m_mutex.lock();
    if (m_tasks.size()) {
        CopyTask task = m_tasks.dequeue();
        m_mutex.unlock();
        process_task(task);
        return;
    }
    m_mutex.unlock();
}

void TAudioFileCopyConvert::process_task(CopyTask task)
{
    emit taskStarted(task.readsource->get_name());

    TAudioBuffer fileIOBuffer;

    task.spec->set_export_start_location(TTimeRef());
    task.spec->set_export_end_location(task.readsource->get_length());

    task.spec->set_export_dir(task.dir);
    task.spec->set_file_format(TraversoDAW::FileFormat::WAV);
    task.spec->set_channel_count(task.readsource->get_channel_count());
    task.spec->set_sample_rate(task.readsource->get_sample_rate());
    task.spec->set_export_file_name(task.outFileName);

    TBufferedAudioStreamWriter* writesource = new TBufferedAudioStreamWriter(task.spec->get_export_dir(), task.spec->get_export_file_name());
    bool failedToPrepareWritesource = false;

    // The frames come at the device's rate (T5)
    if (writesource->prepare_file(task.spec, audiodevice().get_sample_rate()) == -1) {
        failedToPrepareWritesource = true;
        goto out;
    }

    writesource->set_process_peaks(true);

    do {
        if (m_stopProcessing) {
            goto out;
        }

        nframes_t diff = task.spec->get_remaining_export_frames();
        nframes_t this_nframes = std::min(diff, task.spec->get_block_size());
        nframes_t nframes = this_nframes;

        // The reader makes room for its channels; the export may ask for more
        fileIOBuffer.check_capacity(nframes, task.spec->get_channel_count());

        // Read errors are not handled yet (task T10): zeros in the copy
        std::ignore = task.readsource->file_read(fileIOBuffer, task.spec->get_export_location(), nframes);

        TQueueBufferSlot virtualSlot(0, task.spec->get_channel_count(), nframes);

        virtualSlot.update_location_and_offset(TTimeRef(), TTimeRef());
        for (uint chan = 0; chan < task.spec->get_channel_count(); ++chan) {
            virtualSlot.write_channel(chan, fileIOBuffer.get_channel_buffer(chan), 0, nframes);
        }

        writesource->rb_file_write(&virtualSlot, fileIOBuffer);

        task.spec->add_exported_range(TTimeRef(nframes, task.readsource->get_sample_rate()));

    } while (task.spec->get_remaining_export_frames() > 0);

out:
    if (!failedToPrepareWritesource) {
        writesource->finish_file();
    }
    delete writesource;
    writesource = nullptr;
    resources_manager()->remove_source(task.readsource);

    if (m_stopProcessing) {
        exit(0);
        wait(1000);
        m_tasks.clear();
        emit processingStopped();
        return;
    }

    emit taskFinished(task.dir + "/" + task.outFileName + ".wav", task.tracknumber, task.trackname);
}

void TAudioFileCopyConvert::stop_merging()
{
    m_stopProcessing = true;
}
