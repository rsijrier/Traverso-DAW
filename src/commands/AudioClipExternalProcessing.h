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

#ifndef AUDIOCLIP_EXTERNAL_PROCESSING_H
#define AUDIOCLIP_EXTERNAL_PROCESSING_H

#include "TCommand.h"

#include <memory>

class TAudioClip;
class TAudioTrack;
class QUndoCommand;

class AudioClipExternalProcessing : public TCommand
{
        Q_OBJECT

public :
	AudioClipExternalProcessing(TAudioClip* object);
	~AudioClipExternalProcessing();

	int prepare_actions();
	int do_action();
	int undo_action();

	int begin_hold();
	int finish_hold();

	bool is_hold_command() const {return false;}

// private :
	TAudioTrack* m_track;
	TAudioClip* m_clip;
	TAudioClip* m_resultingclip;
    std::unique_ptr<QUndoCommand> m_mutation;

// 	friend class ExternalProcessingDialog;

};

#endif

//eof
