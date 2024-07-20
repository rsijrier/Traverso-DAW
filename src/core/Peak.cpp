/*
Copyright (C) 2005-2007 Remon Sijrier

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

#include "Peak.h"

#include "AbstractAudioReader.h" // Needed for DecodeBuffer declaration
#include "Project.h"
#include "ProjectManager.h"
#include "ReadSource.h"
#include "ResourcesManager.h"
#include "Utils.h"
#include "defines.h"
#include "Mixer.h"
#include "FileHelpers.h"
#include <QFileInfo>
#include <QDateTime>
#include <QMutexLocker>

#include "Debugger.h"

#define NORMALIZE_CHUNK_SIZE	10000
#define PEAKFILE_MAJOR_VERSION	1
#define PEAKFILE_MINOR_VERSION	4

int Peak::zoomStep[] = {
    // non-cached zoomlevels.
    1, 2, 4, 8, 12, 16, 24, 32,
    // Cached zoomlevels
    64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576
};

QHash<int, int> Peak::chacheIndexLut;

typedef short peak_data_t;

Peak::Peak(AudioSource* source)
{
    PENTERCONS;

    m_peaksAvailable = m_permanentFailure = m_interuptPeakBuild = false;

    QString sourcename = source->get_name();
    QString path;
    Project* project = pm().get_project();
    if (project) {
        path = project->get_root_dir() + "/peakfiles/";
    } else {
        path = source->get_dir();
        path = path.replace("audiosources", "peakfiles");
    }

    for (uint chan = 0; chan < source->get_channel_count(); ++ chan) {
        ChannelData* data = new Peak::ChannelData;

        data->fileName = sourcename + "-ch" + QByteArray::number(chan) + ".peak";
        data->fileName.prepend(path);
        data->pd = nullptr;
        data->peakreader = nullptr;

        m_channelData.append(data);
    }

    ReadSource* rs = qobject_cast<ReadSource*>(source);

    if (rs) {
        // This Peak object was created by AudioClip, meant for reading peak data
        m_source = resources_manager()->get_readsource(rs->get_id());
    } else {
        // No ReadSource object? Then it's created by WriteSource for on the fly
        // peak data creation, no m_source needed!
        m_source = nullptr;
    }
}

Peak::~Peak()
{
    PENTERDES;

    delete m_source;

    foreach(ChannelData* data, m_channelData) {
        if (data->normFile.isOpen()) {
            QFile::remove(data->normFileName);
        }

        delete data->peakreader;
        delete data;
    }
}

void Peak::close()
{
    pp().free_peak(this);
}

int Peak::read_header()
{
    PENTER;

    Q_ASSERT(m_source);

    foreach(ChannelData* data, m_channelData) {

        data->file.setFileName(data->fileName);

        if (! data->file.open(QIODevice::ReadOnly)) {
            if (QFile::exists(data->fileName)) {
                m_permanentFailure = true;
            }

            QString errorstring = FileHelper::fileerror_to_string(data->file.error());
            qWarning("Couldn't open peak file for reading! (%s, Error: %s)", QS_C(data->fileName), QS_C(errorstring));
            return -1;
        }

        QFileInfo file(m_source->get_filename());
        QFileInfo peakFile(data->fileName);

        QDateTime fileModTime = file.lastModified();
        QDateTime peakModTime = peakFile.lastModified();

        if (fileModTime > peakModTime) {
            PERROR("Source and Peak file modification time do not match");
            printf("SourceFile modification time is %s\n", fileModTime.toString().toLatin1().data());
            printf("PeakFile modification time is %s\n", peakModTime.toString().toLatin1().data());
            return -1;
        }


        data->file.seek(0);

        data->file.read(data->headerdata.label, sizeof(data->headerdata.label));
        data->file.read(reinterpret_cast<char*>(data->headerdata.version), sizeof(data->headerdata.version));

        if ((data->headerdata.label[0]!='T') ||
                (data->headerdata.label[1]!='R') ||
                (data->headerdata.label[2]!='A') ||
                (data->headerdata.label[3]!='V') ||
                (data->headerdata.label[4]!='P') ||
                (data->headerdata.label[5]!='F') ||
                (data->headerdata.version[0] != PEAKFILE_MAJOR_VERSION) ||
                (data->headerdata.version[1] != PEAKFILE_MINOR_VERSION)) {
            printf("This file either isn't a Traverso Peak file, or the version doesn't match!\n");
            data->file.close();
            return -1;
        }

        data->file.read((char*)data->headerdata.peakDataOffsets, sizeof(data->headerdata.peakDataOffsets));
        data->file.read((char*)data->headerdata.peakDataSizeForLevel, sizeof(data->headerdata.peakDataSizeForLevel));
        data->file.read((char*)&data->headerdata.normValuesDataOffset, sizeof(data->headerdata.normValuesDataOffset));
        data->file.read((char*)&data->headerdata.headerSize, sizeof(data->headerdata.headerSize));

        data->peakreader = new PeakDataReader(data);
        data->peakdataDecodeBuffer = new DecodeBuffer;
    }

    m_peaksAvailable = true;

    return 1;
}

int Peak::write_header(ChannelData* data)
{
    PENTER;

    data->file.seek(0);

    data->headerdata.label[0] = 'T';
    data->headerdata.label[1] = 'R';
    data->headerdata.label[2] = 'A';
    data->headerdata.label[3] = 'V';
    data->headerdata.label[4] = 'P';
    data->headerdata.label[5] = 'F';
    data->headerdata.version[0] = PEAKFILE_MAJOR_VERSION;
    data->headerdata.version[1] = PEAKFILE_MINOR_VERSION;

    data->file.write((char*)data->headerdata.label, sizeof(data->headerdata.label));
    data->file.write((char*)data->headerdata.version, sizeof(data->headerdata.version));
    data->file.write((char*)data->headerdata.peakDataOffsets, sizeof(data->headerdata.peakDataOffsets));
    data->file.write((char*)data->headerdata.peakDataSizeForLevel, sizeof(data->headerdata.peakDataSizeForLevel));
    data->file.write((char*) &data->headerdata.normValuesDataOffset, sizeof(data->headerdata.normValuesDataOffset));
    data->file.write((char*) &data->headerdata.headerSize, sizeof(data->headerdata.headerSize));

    return 1;
}


void Peak::start_peak_loading()
{
    pp().queue_task(this);
}


int Peak::calculate_peaks(
        int chan,
        float ** buffer,
        const TTimeRef &startlocation,
        int peakDataCount,
        qreal framesPerPeak)
{
    PENTER3;

    if (m_permanentFailure) {
        return PERMANENT_FAILURE;
    }

    if(!m_peaksAvailable) {
        if (read_header() < 0) {
            return NO_PEAK_FILE;
        }
    }

    if (peakDataCount <= 0) {
        return NO_PEAKDATA_FOUND;
    }

    ChannelData* data = m_channelData.at(chan);
    int produced = 0;

    // 	PROFILE_START;

    // Macro view mode
    if (framesPerPeak >= 64) {
        int highbit;
        unsigned long nearestpow2 = nearest_power_of_two(qRound(framesPerPeak), highbit);
        if (nearestpow2 == 0) {
            return NO_PEAKDATA_FOUND;
        }


        nframes_t startPos = TTimeRef::to_frame(startlocation, 44100);

        int index = cache_index_lut()->value(nearestpow2, -1);
        if(index >= 0) {
            // 			printf("index %d\n", index);
        }

        int offset = qRound(float(startPos) / nearestpow2) * 2;
        int truncate = 0;

        // Check if this zoom level has as many data as requested.
        if ( (peakDataCount + offset) > data->headerdata.peakDataSizeForLevel[index]) {
            truncate = peakDataCount - (data->headerdata.peakDataSizeForLevel[index] - offset);
            //FIXME: nothing done here?
                       qDebug("Peak::calculate_peaks truncate: %d", truncate);
            //            peakDataCount = data->headerdata.peakDataSizeForLevel[index] - offset;
        }

        nframes_t readposition = data->headerdata.headerSize + (data->headerdata.peakDataOffsets[index] + offset) * sizeof(peak_data_t);
        produced = data->peakreader->read_from(data->peakdataDecodeBuffer, readposition, peakDataCount);

        if (produced != peakDataCount) {
            //			PERROR("Could not read in all peak data, peakDataCount is %d, read count is %d", peakDataCount, produced);
        }

        // 		PROFILE_END("Peak calculate_peaks");

        if (produced == 0) {
            return NO_PEAKDATA_FOUND;
        }

        // 		for (int i=(pixelcount-truncate); i<(pixelcount); ++i) {
        // 			data->peakdataDecodeBuffer->destination[0][i] = 0;
        // 		}
        //
        *buffer = data->peakdataDecodeBuffer->destination[0];

        return produced;

        // Micro view mode
    }
    // Calculate the amount of frames to be read
    nframes_t toRead = qRound(peakDataCount * framesPerPeak * qreal(m_source->get_file_rate()) / qreal(44100));

    nframes_t readFrames = m_source->file_read(data->peakdataDecodeBuffer, startlocation, toRead);

    if (readFrames == 0) {
        return NO_PEAKDATA_FOUND;
    }

    if ( readFrames != toRead) {
        PWARN(QString("Unable to read nframes %1 (only %2 available)").arg(toRead).arg(readFrames).toLatin1().data());
    }

    int count = 0;
    audio_sample_t sample;

    // MicroView needs a buffer to store the calculated peakdata
    // our decodebuffer's readbuffer is large enough for this purpose
    // and it's no problem to use it at this point in the process chain.
    float* peakdata = data->peakdataDecodeBuffer->readBuffer;

    ProcessData pd;
    // the stepSize depends on the real file sample rate, Peak assumes 44100 Hz
    // so if the file sample rate differs, the stepSize becomes the ratio of
    // the file sample rate and 44100
    pd.stepSize = TTimeRef(qreal(44100) / m_source->get_file_rate(), m_source->get_file_rate());
    pd.processRange = TTimeRef(framesPerPeak, m_source->get_file_rate());

    for (uint i=0; i < readFrames; i++) {

        pd.processLocation += pd.stepSize;

        sample = data->peakdataDecodeBuffer->destination[chan][i];

        pd.normValue = f_max(pd.normValue, fabsf(sample));

        if (sample > pd.peakUpperValue) {
            pd.peakUpperValue = sample;
        }

        if (sample < pd.peakLowerValue) {
            pd.peakLowerValue = sample;
        }

        if (pd.processLocation >= pd.nextDataPointLocation) {

            if (pd.peakUpperValue > fabs(pd.peakLowerValue)) {
                peakdata[count] = pd.peakUpperValue;
            } else {
                peakdata[count] = pd.peakLowerValue;
            }

            pd.peakUpperValue = -10.0;
            pd.peakLowerValue = 10.0;

            pd.nextDataPointLocation += pd.processRange;
            count++;
        }
    }

    // 		printf("framesPerPeak, peakDataCount, generated, readFrames %f, %d, %d, %d\n", framesPerPeak, peakDataCount, count, readFrames);

    // 		PROFILE_END("Peak calculate_peaks");

    // Assign the supplied buffer to the 'real' peakdata buffer.
    *buffer = peakdata;

    return count;
}


int Peak::prepare_processing(uint rate)
{
    PENTER;

    foreach(ChannelData* data, m_channelData) {

        data->normFileName = data->fileName;
        data->normFileName.append(".norm");

        // Create read/write enabled file
        data->file.setFileName(data->fileName);

        if (! data->file.open(QIODevice::ReadWrite)) {
            PWARN(QString("Couldn't open peak file for writing! (%1)").arg(data->fileName).toLatin1().data());
            m_permanentFailure  = true;
            return -1;
        }

        // Create the temporary normalization data file
        data->normFile.setFileName(data->normFileName);

        if (! data->normFile.open(QIODevice::ReadWrite)) {
            PWARN(QString("Couldn't open normalization data file for writing! (%1)").arg(data->normFileName).toLatin1().data());
            m_permanentFailure  = true;
            return -1;
        }

        // We need to know the headerSize.
        data->headerdata.headerSize =
                sizeof(data->headerdata.label) +
                sizeof(data->headerdata.version) +
                sizeof(data->headerdata.peakDataOffsets) +
                sizeof(data->headerdata.peakDataSizeForLevel) +
                sizeof(data->headerdata.normValuesDataOffset) +
                sizeof(data->headerdata.headerSize);

        // Now seek to the start position, so we can write the peakdata to it in the process function
        data->file.seek(data->headerdata.headerSize);

        data->pd = new Peak::ProcessData;
        data->pd->stepSize = TTimeRef(nframes_t(1), rate);
        data->pd->processRange = TTimeRef(nframes_t(64), 44100);
    }


    return 1;
}


int Peak::finish_processing()
{
    PENTER;

    foreach(ChannelData* data, m_channelData) {

        if (data->pd->processLocation < data->pd->nextDataPointLocation) {
            peak_data_t peakvalue = (peak_data_t)(data->pd->peakUpperValue * MAX_DB_VALUE);
            data->file.write((char*)&peakvalue, sizeof(peak_data_t));
            peakvalue = (peak_data_t)(-1 * data->pd->peakLowerValue * MAX_DB_VALUE);
            data->file.write((char*)&peakvalue, sizeof(peak_data_t));
            data->pd->processBufferSize += 2;
        }

        int totalBufferSize = 0;

        data->headerdata.peakDataSizeForLevel[0] = data->pd->processBufferSize;
        totalBufferSize += data->pd->processBufferSize;

        for( int i = SAVING_ZOOM_FACTOR + 1; i < ZOOM_LEVELS+1; ++i) {
            data->headerdata.peakDataSizeForLevel[i - SAVING_ZOOM_FACTOR] = data->headerdata.peakDataSizeForLevel[i - SAVING_ZOOM_FACTOR - 1] / 2;
            totalBufferSize += data->headerdata.peakDataSizeForLevel[i - SAVING_ZOOM_FACTOR];
        }


        data->file.seek(data->headerdata.headerSize);

        // The routine below uses a different total buffer size calculation
        // which might end up with a size >= totalbufferSize !!!
        // Need to look into that, for now + 2 seems to work...
        peak_data_t* saveBuffer = new peak_data_t[totalBufferSize + 1*sizeof(peak_data_t)];

        int read = data->file.read((char*)saveBuffer, sizeof(peak_data_t) * data->pd->processBufferSize) / sizeof(peak_data_t);

        if (read != data->pd->processBufferSize) {
            //			PERROR("couldn't read in all saved data?? (%d read)", read);
        }


        int prevLevelBufferPos = 0;
        int nextLevelBufferPos;
        data->headerdata.peakDataSizeForLevel[0] = data->pd->processBufferSize;
        data->headerdata.peakDataOffsets[0] = 0;

        for (int i = SAVING_ZOOM_FACTOR+1; i < ZOOM_LEVELS+1; ++i) {

            int prevLevelSize = data->headerdata.peakDataSizeForLevel[i - SAVING_ZOOM_FACTOR - 1];
            data->headerdata.peakDataOffsets[i - SAVING_ZOOM_FACTOR] = data->headerdata.peakDataOffsets[i - SAVING_ZOOM_FACTOR - 1] + prevLevelSize;
            prevLevelBufferPos = data->headerdata.peakDataOffsets[i - SAVING_ZOOM_FACTOR - 1];
            nextLevelBufferPos = data->headerdata.peakDataOffsets[i - SAVING_ZOOM_FACTOR];


            int count = 0;

            do {
                Q_ASSERT(nextLevelBufferPos <= totalBufferSize);
                saveBuffer[nextLevelBufferPos] = (peak_data_t) f_max(saveBuffer[prevLevelBufferPos], saveBuffer[prevLevelBufferPos + 2]);
                saveBuffer[nextLevelBufferPos + 1] = (peak_data_t) f_max(saveBuffer[prevLevelBufferPos + 1], saveBuffer[prevLevelBufferPos + 3]);
                nextLevelBufferPos += 2;
                prevLevelBufferPos += 4;
                count+=4;
            }
            while (count < prevLevelSize);
        }

        data->file.seek(data->headerdata.headerSize);

        int written = data->file.write((char*)saveBuffer, sizeof(peak_data_t) * totalBufferSize) / sizeof(peak_data_t);

        if (written != totalBufferSize) {
            //			PERROR("could not write complete buffer! (only %d)", written);
            // 		return -1;
        }

        data->normFile.seek(0);

        read = data->normFile.read((char*)saveBuffer, sizeof(audio_sample_t) * data->pd->normDataCount) / sizeof(audio_sample_t);

        if (read != data->pd->normDataCount) {
            //			PERROR("Could not read in all (%d) norm. data, only %d", data->pd->normDataCount, read);
        }

        data->headerdata.normValuesDataOffset = data->headerdata.headerSize + totalBufferSize * sizeof(peak_data_t);

        data->normFile.close();

        if (!QFile::remove(data->normFileName)) {
            //			PERROR("Failed to remove temp. norm. data file! (%s)", data->normFileName.toLatin1().data());
        }

        written = data->file.write((char*)saveBuffer, sizeof(audio_sample_t) * read) / sizeof(audio_sample_t);

        write_header(data);

        data->file.close();

        delete [] saveBuffer;
        delete data->pd;
        data->pd = nullptr;

    }

    emit finished();

    return 1;

}


void Peak::process(uint channel, const audio_sample_t* buffer, nframes_t nframes)
{
    ChannelData* data = m_channelData.at(channel);
    ProcessData* pd = data->pd;

    for (uint i=0; i < nframes; i++) {

        pd->processLocation += pd->stepSize;

        audio_sample_t sample = buffer[i];

        pd->normValue = f_max(pd->normValue, fabsf(sample));

        if (sample > pd->peakUpperValue) {
            pd->peakUpperValue = sample;
        }

        if (sample < pd->peakLowerValue) {
            pd->peakLowerValue = sample;
        }

        if (pd->processLocation >= pd->nextDataPointLocation) {

            peak_data_t peakbuffer[2];

            peakbuffer[0] = (peak_data_t) (pd->peakUpperValue * MAX_DB_VALUE );
            peakbuffer[1] = (peak_data_t) (-1 * (pd->peakLowerValue * MAX_DB_VALUE ));

            int written = data->file.write((char*)peakbuffer, sizeof(peak_data_t) * 2) / sizeof(peak_data_t);

            if (written != 2) {
                PWARN(QString("couldnt write peak data, only (%1)").arg(written).toLatin1().data());
            }

            pd->peakUpperValue = -10.0;
            pd->peakLowerValue = 10.0;

            pd->processBufferSize+=2;
            pd->nextDataPointLocation += pd->processRange;
        }

        if (pd->normProcessedFrames == NORMALIZE_CHUNK_SIZE) {
            int written = data->normFile.write((char*)&pd->normValue, sizeof(audio_sample_t)) / sizeof(audio_sample_t);

            if (written != 1) {
                PWARN(QString("couldnt write norm data, only (%1)").arg(written).toLatin1().data());
            }

            pd->normValue = 0.0;
            pd->normProcessedFrames = 0;
            pd->normDataCount++;
        }

        pd->normProcessedFrames++;
    }
}


int Peak::create_from_scratch()
{
    PENTER;

    // PROFILE_START;

    int ret = -1;

    if (prepare_processing(m_source->get_file_rate()) < 0) {
        return ret;
    }

    nframes_t readFrames = 0;
    nframes_t totalReadFrames = 0;

    nframes_t bufferSize = 65536;

    int progression = 0;

    if (m_source->get_length() == TTimeRef()) {
        qWarning("Peak::create_from_scratch() : m_source (%s) has length 0", m_source->get_name().toLatin1().data());
        return ret;
    }

    if (m_source->get_nframes() < bufferSize) {
        bufferSize = 64;
        if (m_source->get_nframes() < bufferSize) {
            qDebug("source length is too short to display one pixel of the audio wave form in macro view");
            return ret;
        }
    }

    DecodeBuffer decodebuffer;

    do {
        if (m_interuptPeakBuild) {
            ret = -1;
            goto out;
        }

        readFrames = m_source->file_read(&decodebuffer, totalReadFrames, bufferSize);

        if (readFrames <= 0) {
            PERROR("readFrames < 0 during peak building");
            break;
        }

        for (uint chan = 0; chan < m_source->get_channel_count(); ++ chan) {
            process(chan, decodebuffer.destination[chan], readFrames);
        }

        totalReadFrames += readFrames;
        progression = (int) ((float)totalReadFrames / ((float)m_source->get_nframes() / 100.0));

        ChannelData* data = m_channelData.at(0);

        if ( progression > data->pd->progress) {
            emit progress(progression);
            data->pd->progress = progression;
        }
    } while (totalReadFrames < m_source->get_nframes());


    if (finish_processing() < 0) {
        ret = -1;
        goto out;
    }

    ret = 1;

out:

    // 	PROFILE_END("Peak create from scratch");

    return ret;
}


audio_sample_t Peak::get_max_amplitude(const TTimeRef &startlocation, const TTimeRef &endlocation)
{
    foreach(ChannelData* data, m_channelData) {
        if (!data->file.isOpen() || !m_peaksAvailable) {
            printf("either the file is not open, or no peak data available\n");
            return 0.0f;
        }
    }
    int rate = m_source->get_file_rate();
    nframes_t startframe = TTimeRef::to_frame(startlocation, rate);
    nframes_t endframe = TTimeRef::to_frame(endlocation, rate);
    int startpos = startframe / NORMALIZE_CHUNK_SIZE;
    uint count = (endframe / NORMALIZE_CHUNK_SIZE) - startpos;

    uint buffersize = count < NORMALIZE_CHUNK_SIZE*2 ? NORMALIZE_CHUNK_SIZE*2 : count;
    audio_sample_t* readbuffer =  new audio_sample_t[buffersize];

    audio_sample_t maxamp = 0;
    DecodeBuffer decodebuffer;
    // Read in the part not fully occupied by a cached normalize value
    // at the left hand part and run compute_peak on it.
    if (startframe != 0) {
        startpos += 1;
        int toRead = (int) ((startpos * NORMALIZE_CHUNK_SIZE) - startframe);

        int read = m_source->file_read(&decodebuffer, startframe, toRead);

        for (uint chan = 0; chan < m_source->get_channel_count(); ++ chan) {
            maxamp = Mixer::compute_peak(decodebuffer.destination[chan], read, maxamp);
        }
    }


    // Read in the part not fully occupied by a cached normalize value
    // at the right hand part and run compute_peak on it.
    float f = (float) endframe / NORMALIZE_CHUNK_SIZE;
    int endpos = (int) f;
    int toRead = (int) ((f - (endframe / float(NORMALIZE_CHUNK_SIZE))) * NORMALIZE_CHUNK_SIZE);
    int read = m_source->file_read(&decodebuffer, endframe - toRead, toRead);

    if (read > 0) {
        for (uint chan = 0; chan < m_source->get_channel_count(); ++ chan) {
            maxamp = Mixer::compute_peak(decodebuffer.destination[chan], read, maxamp);
        }
    }

    // Now that we have covered both boundary situations,
    // read in the cached normvalues, and calculate the highest value!
    count = endpos - startpos;

    foreach(ChannelData* data, m_channelData) {
        data->file.seek(data->headerdata.normValuesDataOffset + (startpos * sizeof(audio_sample_t)));

        int read = data->file.read((char*)readbuffer, sizeof(audio_sample_t) * count) / sizeof(audio_sample_t);

        if (read != (int)count) {
            printf("Peak::get_max_amplitude: could only read %d, %d requested\n", read, count);
        }

        maxamp = Mixer::compute_peak(readbuffer, read, maxamp);
    }

    delete [] readbuffer;

    return maxamp;
}




/******** PEAK BUILD THREAD CLASS **********/
/******************************************/

PeakProcessor& pp()
{
    static PeakProcessor processor;
    return processor;
}


PeakProcessor::PeakProcessor()
{
    m_ppthread = new PPThread(this);
    m_taskRunning = false;
    m_runningPeak = 0;

    moveToThread(m_ppthread);

    m_ppthread->start();

    connect(this, SIGNAL(newTask()), this, SLOT(start_task()), Qt::QueuedConnection);
}


PeakProcessor::~ PeakProcessor()
{
    m_ppthread->exit(0);

    if (!m_ppthread->wait(1000)) {
        m_ppthread->terminate();
    }

    delete m_ppthread;
}


void PeakProcessor::start_task()
{
    m_runningPeak->create_from_scratch();

    QMutexLocker locker(&m_mutex);

    m_taskRunning = false;

    if (m_runningPeak->m_interuptPeakBuild) {
        PMESG("PeakProcessor:: Deleting interrupted Peak!");
        delete m_runningPeak;
        m_runningPeak = nullptr;
        m_wait.wakeAll();
        return;
    }

    foreach(Peak* peak, m_queue) {
        if (m_runningPeak->m_source->get_filename() == peak->m_source->get_filename()) {
            m_queue.removeAll(peak);
            emit peak->finished();
        }
    }

    m_runningPeak = 0;

    dequeue_queue();
}

void PeakProcessor::queue_task(Peak * peak)
{
    QMutexLocker locker(&m_mutex);

    m_queue.enqueue(peak);

    if (!m_taskRunning) {
        dequeue_queue();
    }
}

void PeakProcessor::dequeue_queue()
{
    if (!m_queue.isEmpty()) {
        m_taskRunning = true;
        m_runningPeak = m_queue.dequeue();
        emit newTask();
    }
}

void PeakProcessor::free_peak(Peak * peak)
{
    m_mutex.lock();

    m_queue.removeAll(peak);

    if (peak == m_runningPeak) {
        PMESG("PeakProcessor:: Interrupting running build process!");
        peak->m_interuptPeakBuild =  true;

        PMESG("PeakProcessor:: Waiting GUI thread until interrupt finished");
        m_wait.wait(&m_mutex);
        PMESG("PeakProcessor:: Resuming GUI thread");

        dequeue_queue();

        m_mutex.unlock();

        return;
    }

    m_mutex.unlock();

    delete peak;
}


PPThread::PPThread(PeakProcessor * pp)
{
    m_pp = pp;
}

void PPThread::run()
{
    exec();
}



PeakDataReader::PeakDataReader(Peak::ChannelData* data)
{
    m_d = data;
    m_nframes = m_d->file.size();
    m_readPos = 0;
}


nframes_t PeakDataReader::read_from(DecodeBuffer* buffer, nframes_t start, nframes_t count)
{
    // 	printf("read_from:: before_seek from %d, framepos is %d\n", start, m_readPos);

    if (!seek(start)) {
        return 0;
    }

    return read(buffer, count);
}


bool PeakDataReader::seek(nframes_t start)
{
    if (m_readPos != start) {
        Q_ASSERT(m_d->file.isOpen());


        if (start >= m_nframes) {
            return false;
        }

        if (!m_d->file.seek(start)) {
            qWarning("PeakDataReader: could not seek to data point %d within %s", start, QS_C(m_d->fileName));
            return false;
        }

        m_readPos = start;
    }

    return true;
}


nframes_t PeakDataReader::read(DecodeBuffer* buffer, nframes_t count)
{
    if ( ! (count && (m_readPos < m_nframes)) ) {
        return 0;
    }

    // Make sure the read buffer is big enough for this read
    buffer->check_buffers_capacity(count*3, 1);

    Q_ASSERT(m_d->file.isOpen());

    qint64 framesRead = 0;
    peak_data_t* readbuffer;

    qint64 length = sizeof(peak_data_t) * count;
    framesRead = m_d->file.read(reinterpret_cast<char*>(buffer->readBuffer), length) / qint64(sizeof(peak_data_t));
    readbuffer = reinterpret_cast<peak_data_t*>(buffer->readBuffer);

    for (int f = 0; f < framesRead; f++) {
        buffer->destination[0][f] = float(readbuffer[f]);
    }

    m_readPos += framesRead;

    return nframes_t(framesRead);
}

void Peak::calculate_lut_data()
{
    chacheIndexLut.insert(64     , 0);
    chacheIndexLut.insert(128    , 1);
    chacheIndexLut.insert(256    , 2);
    chacheIndexLut.insert(512    , 3);
    chacheIndexLut.insert(1024   , 4);
    chacheIndexLut.insert(2048   , 5);
    chacheIndexLut.insert(4096   , 6);
    chacheIndexLut.insert(8192   , 7);
    chacheIndexLut.insert(16384  , 8);
    chacheIndexLut.insert(32768  , 9);
    chacheIndexLut.insert(65536  , 10);
    chacheIndexLut.insert(131072 , 11);
    chacheIndexLut.insert(262144 , 12);
    chacheIndexLut.insert(524288 , 13);
    chacheIndexLut.insert(1048576, 14);
}

int Peak::max_zoom_value()
{
    return 1048576;
}

Peak::ChannelData::~ ChannelData()
{

    delete peakdataDecodeBuffer;

}
