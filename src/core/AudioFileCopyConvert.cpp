/*
Copyright (C) 2007 Remon Sijrier 

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

#include "AudioFileCopyConvert.h"
#include <QFile>
#include <QMutexLocker>
#include <QFileInfo>

#include "TExportSpecification.h"
#include "AbstractAudioReader.h"
#include "ProjectManager.h"
#include "ResourcesManager.h"
#include "ReadSource.h"
#include "WriteSource.h"
#include "Peak.h"
#include "defines.h"

AudioFileCopyConvert::AudioFileCopyConvert()
{
	m_stopProcessing = false;
	moveToThread(this);
	start();
	connect(this, SIGNAL(dequeueTask()), this, SLOT(dequeue_tasks()), Qt::QueuedConnection);
}

/**
 *	Queues the ReadSource source to be copied. This function will take ownership of the ReadSource
	and takes care of 'deleting' it once the copy is finished!!

 * @param source 
 * @param dir 
 * @param outfilename 
 * @param tracknumber 
 * @param trackname
 */
void AudioFileCopyConvert::enqueue_task(ReadSource * source,
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

void AudioFileCopyConvert::dequeue_tasks()
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

void AudioFileCopyConvert::process_task(CopyTask task)
{
	emit taskStarted(task.readsource->get_name());

	DecodeBuffer decodebuffer;

    task.spec->set_export_start_location(TTimeRef());
    task.spec->set_export_end_location(task.readsource->get_length());

    task.spec->set_export_dir(task.dir);
	task.spec->extraFormat["filetype"] = "wav";
    task.spec->set_channel_count(task.readsource->get_channel_count());
    task.spec->set_sample_rate(task.readsource->get_sample_rate());
    task.spec->set_export_file_name(task.outFileName);
	
	WriteSource* writesource = new WriteSource(task.spec);
	bool failedToPrepareWritesource = false;

	if (writesource->prepare_export() == -1) {
		failedToPrepareWritesource = true;
		goto out;
	}
	// Enable on the fly generation of peak data to speedup conversion 
	// (no need to re-read all the audio files to generate peaks)
	writesource->set_process_peaks(true);
	
	do {
		// if the user asked to stop processing, jump out of this 
		// loop, and cleanup any resources in use.
		if (m_stopProcessing) {
			goto out;
		}
			
        nframes_t diff = task.spec->get_remaining_export_frames();
        nframes_t this_nframes = std::min(diff, task.spec->get_block_size());
		nframes_t nframes = this_nframes;

        task.spec->silence_render_buffer(nframes);

        task.readsource->file_read(&decodebuffer, task.spec->get_export_location(), nframes);
			
		for (uint x = 0; x < nframes; ++x) {
            for (uint y = 0; y < task.spec->get_channel_count(); ++y) {
                task.spec->get_render_buffer()[y + x*task.spec->get_channel_count()] = decodebuffer.destination[y][x];
			}
		}
		
		// due the fact peak generating does _not_ happen in writesource->process
		// but in a function used by DiskIO, we have to hack the peak processing 
		// in here.
        for (uint y = 0; y < task.spec->get_channel_count(); ++y) {
			writesource->get_peak()->process(y, decodebuffer.destination[y], nframes);
		}
		
		// Process the data, and write to disk
        // FIXME
        // Shouldn't we use the actual read frames count instead of block size?
        // The end of the file will be most likely not a multiple of block size.
        writesource->process(task.spec->get_block_size());
		
        task.spec->add_exported_range(TTimeRef(nframes, task.readsource->get_sample_rate()));

    } while (task.spec->get_remaining_export_frames() > 0);

	
	out:
	if (!failedToPrepareWritesource) {
		writesource->finish_export();
	}
	delete writesource;
    writesource = nullptr;
	resources_manager()->remove_source(task.readsource);
	
	//  The user asked to stop processing, exit the event loop
	// and signal we're done.
	if (m_stopProcessing) {
		exit(0);
		wait(1000);
		m_tasks.clear();
		emit processingStopped();
		return;
	}
	
	emit taskFinished(task.dir + "/" + task.outFileName + ".wav", task.tracknumber, task.trackname);
}

void AudioFileCopyConvert::stop_merging()
{
	m_stopProcessing = true;
}

