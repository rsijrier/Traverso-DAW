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

#include "AbstractAudioWriter.h"
#include "SFAudioWriter.h"
#include "TExportSpecification.h"
#include "WPAudioWriter.h"

#include <QString>

RELAYTOOL_WAVPACK;

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


AbstractAudioWriter::AbstractAudioWriter(TExportSpecification *spec)
    : m_exportSpecification(spec)
{
	m_writePos = 0;
	
	m_isOpen = false;
}


AbstractAudioWriter::~AbstractAudioWriter()
{
}

bool AbstractAudioWriter::set_format_attribute(const QString& key, const QString& value)
{
	Q_UNUSED(key);
	Q_UNUSED(value);
	return false;
}


nframes_t AbstractAudioWriter::pos()
{
	return m_writePos;
}


bool AbstractAudioWriter::open(const QString& filename)
{
	if (m_isOpen) {
		close();
	}
	
	m_writePos = 0;
	m_fileName = filename;
	
	m_isOpen = open_private();
	
	return m_isOpen;
}


bool AbstractAudioWriter::close()
{
	bool success = false;;
	
	if (m_isOpen) {
		success = close_private();
		m_isOpen = false;
	}
	
	return success;
}


nframes_t AbstractAudioWriter::write(void* buffer, nframes_t count)
{
	if (m_isOpen && buffer && count) {
		nframes_t framesWritten = write_private(buffer, count);
		
		if (framesWritten > 0) {
			m_writePos += framesWritten;
		}
		
		return framesWritten;
	}
	
	return 0;
}


// Static method used by other classes to get an AudioWriter for the correct file type
AbstractAudioWriter* AbstractAudioWriter::create_audio_writer(TExportSpecification *spec)
{
    if (spec->get_writer_type() == "sndfile") {
        return new SFAudioWriter(spec);
	}
    else if (libwavpack_is_present && spec->get_writer_type() == "wavpack") {
        return new WPAudioWriter(spec);
	}
	
    return nullptr;
}
