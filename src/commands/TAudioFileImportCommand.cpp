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

#include <QFileDialog>
#include "TAudioFileImportCommand.h"
#include "TAudioClip.h"
#include "TAudioTrack.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TBufferedAudioStreamReader.h"
#include "TResourcesManager.h"
#include "Utils.h"
#include "TMainWindow.h"



#include "Debugger.h"

TAudioFileImportCommand::TAudioFileImportCommand(TContextItem *context)
    : TCommand(context, tr("Import Audio File"))
{
    m_track = nullptr;
    m_fileName = "";
    m_clip = nullptr;
    m_readSource = nullptr;
    m_importLocation = TTimeRef::INVALID;
    m_initialLength = TTimeRef();
    m_silent = false;
    m_hasPosition = false;
    m_name = QString("Audio File Import: No Filename provided");
}


TAudioFileImportCommand::~TAudioFileImportCommand()
= default;

void TAudioFileImportCommand::set_file_name(const QString &fileName)
{
    m_fileName = fileName;
}

void TAudioFileImportCommand::set_track(TAudioTrack * track)
{
    m_track = track;
}


void TAudioFileImportCommand::set_import_location(const TTimeRef& position)
{
    m_hasPosition = true;
    m_importLocation = position;
}

void TAudioFileImportCommand::set_length(const TTimeRef &length)
{
    m_initialLength = length;
}

void TAudioFileImportCommand::set_silent(bool silent)
{
    m_silent = silent;
    setText(tr("Insert Silence"));
}

int TAudioFileImportCommand::create_readsource()
{
	int splitpoint = m_fileName.lastIndexOf("/") + 1;
	int length = m_fileName.length();

	QString dir = m_fileName.left(splitpoint - 1) + "/";
	m_name = m_fileName.right(length - splitpoint);
	
    m_readSource = resources_manager()->import_source(dir, m_name);
    if (! m_readSource) {
//		PERROR("Can't import audiofile %s", QS_C(m_fileName));
		return -1;
	}
	
	return 1;
}

void TAudioFileImportCommand::create_audioclip()
{
	Q_ASSERT(m_track);
	m_clip = resources_manager()->new_audio_clip(m_name);
    resources_manager()->set_source_for_clip(m_clip, m_readSource);
	m_clip->set_sheet(m_track->get_sheet());
	m_clip->set_track(m_track);
	
	TTimeRef startLocation;
    // check if m_importLocation still equals default value
    // it means it was never changed.
    if (m_importLocation == TTimeRef::INVALID) {
        startLocation = m_track->get_end_location();
    } else {
        startLocation = m_importLocation;
	}

    m_clip->move_to_location(startLocation);
	
	if (m_initialLength > qint64(0)) {
		m_clip->set_right_edge(m_initialLength + startLocation);
	}
}

int TAudioFileImportCommand::prepare_actions()
{
    PENTER;
    if (m_silent) {
        m_readSource = resources_manager()->get_silent_readsource();
        m_name = tr("Silence");
        m_fileName = tr("Silence");
        create_audioclip();
        return 1;
    }


    if (m_fileName.isEmpty()) {
        QString allFiles = tr("All files (*)");
        QString activeFilter = tr("Audio files (*.wav *.flac *.ogg *.mp3 *.wv *.w64 *.m4a *.aac)");
        m_fileName = QFileDialog::getOpenFileName(TMainWindow::instance(),
                                                  tr("Import audio source"),
                                                  pm().get_project()->get_import_dir(),
                                                  allFiles + ";;" + activeFilter,
                                                  &activeFilter);

        int splitpoint = m_fileName.lastIndexOf("/") + 1;
        QString dir = m_fileName.left(splitpoint - 1);

        if (m_fileName.isEmpty()) {
            PWARN("Import:: FileName is empty!");
            return -1;
        }

        pm().get_project()->set_import_dir(dir);

        if (create_readsource() == -1) {
            return -1;
        }
        create_audioclip();

        return 1;
    }

    return 1;
}

int TAudioFileImportCommand::do_action()
{
	PENTER;
	
	if (! m_clip) {
		create_audioclip();
	}
    TAudioClipAddRemoveSpec spec;
    spec.set_clip(m_clip);
    spec.set_is_historable(false);
    spec.set_is_move(false);

    TCommand::process_command(m_track->add_clip(spec));
	
	return 1;
}


int TAudioFileImportCommand::undo_action()
{
	PENTER;
    TAudioClipAddRemoveSpec spec;
    spec.set_clip(m_clip);
    spec.set_is_historable(false);
    spec.set_is_move(false);
    TCommand::process_command(m_track->remove_clip(spec));
	return 1;
}


// eof
