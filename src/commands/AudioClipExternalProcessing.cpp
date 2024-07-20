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

#include "AudioClipExternalProcessing.h"

#include "ExternalProcessingDialog.h"

#include "AudioClip.h"
#include "AudioTrack.h"
#include "TMainWindow.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"




AudioClipExternalProcessing::AudioClipExternalProcessing(AudioClip* clip)
	: TCommand(clip, tr("Clip: External Processing"))
{
	m_clip = clip;
	m_resultingclip = 0;
	m_track = m_clip->get_track();
}


AudioClipExternalProcessing::~AudioClipExternalProcessing()
{}


int AudioClipExternalProcessing::prepare_actions()
{
	PENTER;
	ExternalProcessingDialog epdialog(TMainWindow::instance(), this);
	
	epdialog.exec();
	
	if (! m_resultingclip) {
		return -1;
	}
	
	return 1;
}

int AudioClipExternalProcessing::begin_hold()
{
	return 1;
}

int AudioClipExternalProcessing::finish_hold()
{
	return 1;
}

int AudioClipExternalProcessing::do_action()
{
	PENTER;
        // Remove has to be done BEFORE adding, else the TRealTimeLinkedList logic
        // gets messed up for the Tracks AudioClipList, which is an TRealTimeLinkedList :(
	TCommand::process_command(m_track->remove_clip(m_clip, false));
	TCommand::process_command(m_track->add_clip(m_resultingclip, false));
	
	return 1;
}

int AudioClipExternalProcessing::undo_action()
{
	PENTER;
        // Remove has to be done BEFORE adding, else the TRealTimeLinkedList logic
        // gets messed up for the Tracks AudioClipList, which is an TRealTimeLinkedList :(
	TCommand::process_command(m_track->remove_clip(m_resultingclip, false));
	TCommand::process_command(m_track->add_clip(m_clip, false));
	return 1;
}



