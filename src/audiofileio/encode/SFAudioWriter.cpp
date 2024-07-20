/*
Copyright (C) 2007 Ben Levitt 

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

#include "SFAudioWriter.h"
#include "TExportSpecification.h"
#include "Utils.h"

#include <QString>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


SFAudioWriter::SFAudioWriter(TExportSpecification *spec)
    : AbstractAudioWriter(spec)
{
	m_sf = 0;
}


SFAudioWriter::~SFAudioWriter()
{
	if (m_sf) {
        SFAudioWriter::close_private();
	}
}




bool SFAudioWriter::open_private()
{
	char errbuf[256];
	
	memset (&m_sfinfo, 0, sizeof(m_sfinfo));
    m_sfinfo.format = m_exportSpecification->get_data_format() | m_exportSpecification->get_file_format();
    // TODO: Why on earth setting it to 100 seconds of 48Khz amount of frames?
	m_sfinfo.frames = 48000*100;
    m_sfinfo.samplerate = m_exportSpecification->get_sample_rate();
    m_sfinfo.channels = m_exportSpecification->get_channel_count();
    // This commented out line of code has been there since the first upload ofr SFAudioWriter
    // not clear to me what libsndfile does with this frames number?
	//m_sfinfo.frames = m_spec->endLocation - m_spec->startLocation + 1;
	
	m_file.setFileName(m_fileName);
	
	if (!m_file.open(QIODevice::WriteOnly)) {
		qWarning("SFAudioReader::open_private: Could not create file (%s)", QS_C(m_fileName));
		return false;
	}
	
	m_sf = sf_open_fd(m_file.handle(), SFM_WRITE, &m_sfinfo, false);
	
    if (m_sf == nullptr) {
        sf_error_str (nullptr, errbuf, sizeof (errbuf) - 1);
        PWARN(QString("Export: cannot open output file \"%1\" (%2)").arg(m_fileName, errbuf).toLatin1().data());
		return false;
	}
	
	return true;
}


nframes_t SFAudioWriter::write_private(void* buffer, nframes_t frameCount)
{
	int written = 0;
	char errbuf[256];
	
    switch (m_exportSpecification->get_data_format()) {
        case SF_FORMAT_PCM_S8:
            written = sf_write_raw (m_sf, (void*) buffer, frameCount * m_exportSpecification->get_channel_count());
			break;

        case SF_FORMAT_PCM_16:
			written = sf_writef_short (m_sf, (short*) buffer, frameCount);
			break;

        case SF_FORMAT_PCM_24:
        case SF_FORMAT_PCM_32:
			written = sf_writef_int (m_sf, (int*) buffer, frameCount);
			break;

        default: // SF_FORMAT_FLOAT
			written = sf_writef_float (m_sf, (float*) buffer, frameCount);
			break;
	}
	
	if ((nframes_t) written != frameCount) {
		sf_error_str (m_sf, errbuf, sizeof (errbuf) - 1);
//		PERROR("Export: could not write data to output file (%s)\n", errbuf);
		return -1;
	}
	
	return written;
}


bool SFAudioWriter::close_private()
{
	bool success = (sf_close(m_sf) == 0);
	
	m_sf = nullptr;
	
	return success;
}

