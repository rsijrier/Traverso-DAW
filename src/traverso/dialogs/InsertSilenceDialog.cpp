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

#include "InsertSilenceDialog.h"
#include "TProjectManager.h"
#include "TProject.h"
#include "TSheet.h"
#include "TAudioClipFactory.h"
#include "TAudioTrack.h"
#include "TAudioClip.h"

#include <QDialogButtonBox>
#include <QPushButton>

InsertSilenceDialog::InsertSilenceDialog(QWidget * parent)
    : QDialog(parent)
{
    setupUi(this);
    m_track = nullptr;
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
}


void InsertSilenceDialog::setTrack(TAudioTrack* track)
{
    m_track = track;
}

void InsertSilenceDialog::focusInput()
{
    lengthSpinBox->setFocus();
    lengthSpinBox->selectAll();
}

void InsertSilenceDialog::accept()
{
    TSheet* sheet = qobject_cast<TSheet*>(pm().get_project()->get_current_session());
    if (!sheet) {
        return;
    }

    QList<TAudioTrack*> tracks = sheet->get_audio_tracks();

    // Make sure track is still in the sheet
    if (m_track){
        TAudioTrack*	foundTrack = nullptr;

        foreach(TAudioTrack* track, tracks) {
            if (track == m_track) {
                foundTrack = track;
            }
        }
        m_track = foundTrack;
    }

    if (sheet->get_audio_track_count() > 0) {
        if (!m_track){
            TAudioTrack*	shortestTrack = (TAudioTrack*)tracks.first();

            foreach(TAudioTrack* track, tracks) {
                if ( ! (track->get_end_location() > shortestTrack->get_end_location()) ) {
                    shortestTrack = track;
                }
            }
            m_track = shortestTrack;
        }

        // Appended to the end of the track
        TTimeRef length = TTimeRef::seconds_to_timeref(lengthSpinBox->value());
        TAudioClip* clip = TAudioClipFactory::create_silent(m_track, m_track->get_end_location(), length);

        QUndoCommand* command = sheet->create_command_mutate_clips({clip}, {}, tr("Insert Silence"));

        if (QUndoStack* undoStack = sheet->get_history_stack()) {
            undoStack->push(command);
        } else {
            command->redo();
            delete command;
        }
    }

    hide();
}

void InsertSilenceDialog::reject()
{
    hide();
}


//eof

