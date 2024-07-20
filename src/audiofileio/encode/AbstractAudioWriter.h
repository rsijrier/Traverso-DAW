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

#ifndef ABSTRACTAUDIOWRITER_H
#define ABSTRACTAUDIOWRITER_H

#include "defines.h"

class TExportSpecification;

class AbstractAudioWriter
{
	
public:
    AbstractAudioWriter(TExportSpecification* spec);
	virtual ~AbstractAudioWriter();
	
	virtual bool set_format_attribute(const QString& key, const QString& value);
	nframes_t pos();
	
	bool open(const QString& filename);
	nframes_t write(void* buffer, nframes_t frameCount);
	bool close();
	
    static AbstractAudioWriter* create_audio_writer(TExportSpecification* spec);
	
protected:
	virtual bool open_private() = 0;
	virtual nframes_t write_private(void* buffer, nframes_t frameCount) = 0;
	virtual bool close_private() = 0;

    TExportSpecification*   m_exportSpecification;
	QString		m_fileName;
	bool		m_isOpen;
	nframes_t	m_writePos;
};

#endif
