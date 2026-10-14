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
#include <QFile>
#include <QMutexLocker>
#include <QFileInfo>

#include "TExportSpecification.h"
#include "TProjectManager.h"
#include "TQueueBufferSlot.h"
#include "TResourcesManager.h"
#include "TBufferedAudioStreamReader.h"
#include "TFileIOBuffer.h"
#include "TBufferedAudioStreamWriter.h"
#include "TPeak.h"
#include "defines.h"

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

    TFileIOBuffer fileIOBuffer;

    task.spec->set_export_start_location(TTimeRef());
    task.spec->set_export_end_location(task.readsource->get_length());

    task.spec->set_export_dir(task.dir);
    task.spec->set_file_format(TraversoDAW::FileFormat::WAV);
    task.spec->set_channel_count(task.readsource->get_channel_count());
    task.spec->set_sample_rate(task.readsource->get_sample_rate());
    task.spec->set_export_file_name(task.outFileName);

    TBufferedAudioStreamWriter* writesource = new TBufferedAudioStreamWriter(task.spec->get_export_dir(), task.spec->get_export_file_name());
    bool failedToPrepareWritesource = false;

    if (writesource->prepare_export(task.spec) == -1) {
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

        fileIOBuffer.check_capacity(nframes * task.spec->get_channel_count(), task.spec->get_channel_count());

        task.readsource->file_read(fileIOBuffer, task.spec->get_export_location(), nframes);

        TQueueBufferSlot virtualSlot(0, task.spec->get_channel_count(), nframes);

        for (uint chan = 0; chan < task.spec->get_channel_count(); ++chan) {
            float* channelDataPtr = fileIOBuffer.get_channel_buffer(chan).get_data(nframes);
            virtualSlot.write_buffer(TTimeRef(), TTimeRef(), channelDataPtr, chan, nframes);
        }

        writesource->rb_file_write(&virtualSlot, fileIOBuffer);

        task.spec->add_exported_range(TTimeRef(nframes, task.readsource->get_sample_rate()));

    } while (task.spec->get_remaining_export_frames() > 0);

out:
    if (!failedToPrepareWritesource) {
        writesource->finish_export();
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
