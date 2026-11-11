/*
Copyright (C) 2005-2008 Remon Sijrier 

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


#include "ClipSelection.h"
#include "TAudioClipManager.h"
#include <TAudioClip.h>
#include <TSheet.h>

#include "Debugger.h"

ClipSelection::ClipSelection(TAudioClip* clip, QVariantList args)
	: TCommand("")
{
    m_slotName = args.at(0).toString();; // Kept only for error logging text

    if (m_slotName == "remove_from_selection") {
        setText(tr("Selection: Remove Clip"));
        m_methodPointer = &TAudioClipManager::remove_from_selection;
    } else if (m_slotName == "add_to_selection") {
        setText(tr("Selection: Add Clip"));
        m_methodPointer = &TAudioClipManager::add_to_selection;
    } else if (m_slotName == "select_clip") {
        setText(tr("Select Clip"));
        m_methodPointer = &TAudioClipManager::select_clip;
    } else if (m_slotName == "toggle_selected") {
        setText(tr("Toggle Selected Clip"));
        m_methodPointer = &TAudioClipManager::toggle_selected;
    } else {
        PERROR(QString("Unknown slot action passed to ClipSelection: %1").arg(m_slotName));
    }
	
	m_clips.append( clip );
	m_acmanager = clip->get_sheet()->get_audioclip_manager();
}

ClipSelection::ClipSelection( QList< TAudioClip * > clips, TAudioClipManager * manager, const char * slot, const QString& des )
    : TCommand(des)
{
	m_clips = clips;
    m_slotName = slot;
	m_acmanager = manager;
}

ClipSelection::~ClipSelection()
{}

int ClipSelection::do_action()
{
    Q_ASSERT (m_methodPointer);

    for (auto clip : std::as_const(m_clips)) {
        if (!QMetaObject::invokeMethod(m_acmanager, m_methodPointer, Qt::QueuedConnection, clip)) {
            PERROR(QString("AudioClip::%1 failed for %2").arg(m_slotName, clip->get_name()));
        }
    }


    return 1;
}
