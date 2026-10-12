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
 
#include "TAudioClipManager.h"

#include "ClipSelection.h"
#include "TSelectionService.h"
#include "TSheet.h"
#include "TAudioClip.h"
#include "TResourcesManager.h"
#include "TProjectManager.h"
#include "TSnapList.h"
#include "TLocation.h"
#include "Debugger.h"

TAudioClipManager::TAudioClipManager( TSheet* sheet )
	: TContextItem(sheet)
{
	PENTERCONS;
	m_sheet = sheet;
    set_history_stack( m_sheet->get_history_stack() );
	m_lastLocation = TTimeRef();
}

TAudioClipManager::~ TAudioClipManager( )
{
	PENTERDES;
}


QDomNode TAudioClipManager::get_state(QDomDocument doc, bool /*istemplate*/)
{
	QDomElement managerNode = doc.createElement("ClipManager");
	QDomElement globalSelection = doc.createElement("GlobalSelection");
	
	QStringList selectedClips;
	foreach(TAudioClip* clip, m_clipselection) {
		selectedClips << QString::number(clip->get_id());
	}
	
	globalSelection.setAttribute("clips",  selectedClips.join(";"));
	
	managerNode.appendChild(globalSelection);
	
	return managerNode;
}

int TAudioClipManager::set_state(const QDomNode & node)
{
	QDomElement e = node.firstChildElement("GlobalSelection");
	
	QStringList selectionList = e.attribute("clips", "").split(";");
	
	for (int i=0; i<selectionList.size(); ++i) {
		qint64 id = selectionList.at(i).toLongLong();
		foreach(TAudioClip* clip, m_clips) {
			if (clip->get_id() == id) {
				add_to_selection(clip);
			}
		}
	}
	
	return 1;

}


void TAudioClipManager::add_clip( TAudioClip * clip )
{
	PENTER;
	if (m_clips.contains(clip)) {
//		PERROR("Trying to add clip %s, but it's already in my list!!", QS_C(clip->get_name()));
		return;
	}
	
	m_clips.append( clip );
	
    connect(clip->get_location(), &TLocation::locationChanged, this, &TAudioClipManager::update_last_frame);
	
	m_sheet->get_snap_list()->mark_dirty();
	update_last_frame();
	resources_manager()->mark_clip_added(clip);
}

void TAudioClipManager::remove_clip( TAudioClip * clip )
{
	PENTER;
	if (m_clips.removeAll(clip) == 0) {
//		PERROR("Clip %s was not in my list, couldn't remove it!!", QS_C(clip->get_name()));
		return;
	}
	
	remove_from_selection(clip);
	
	m_sheet->get_snap_list()->mark_dirty();
	update_last_frame();
	resources_manager()->mark_clip_removed(clip);
}


void TAudioClipManager::update_last_frame( )
{
        PENTER3;
	
	m_lastLocation = TTimeRef();
	
	foreach(TAudioClip* clip, m_clips) {
        if (clip->get_location_end() >= m_lastLocation)
            m_lastLocation = clip->get_location_end();
	}
	
	emit m_sheet->lastFramePositionChanged();
}

TTimeRef TAudioClipManager::get_last_location() const
{
	return m_lastLocation;
}

void TAudioClipManager::get_selected_clips( QList< TAudioClip * > & list )
{
	foreach(TAudioClip* clip, m_clipselection) {
		list.append(clip);
	}
}

void TAudioClipManager::add_to_selection( TAudioClip * clip )
{
    if ( ! m_clipselection.contains( clip ) ) {
        m_clipselection.append( clip );
        clip->set_selected( true );

        if (m_sheet && m_sheet->get_selection_service()) {
            m_sheet->get_selection_service()->select_with_context(
                clip,
                TSelectionService::ContextTrackLane
                );
        }
    }
}

void TAudioClipManager::remove_from_selection( TAudioClip * clip )
{
    if (m_clipselection.contains(clip)) {
        m_clipselection.removeAll(clip);
        clip->set_selected( false );

        if (m_sheet && m_sheet->get_selection_service()) {
            m_sheet->get_selection_service()->deselect(clip);
        }
    }
}


void TAudioClipManager::toggle_selected( TAudioClip * clip )
{
	PENTER;
	
	if (clip->is_selected()) {
		remove_from_selection(clip);
	} else {
		add_to_selection(clip);
	}
}

void TAudioClipManager::select_clip(TAudioClip* clip)
{
	PENTER;
	
	foreach(TAudioClip* c, m_clips) {
		remove_from_selection(c);
	}
	
	add_to_selection(clip);
}

QList<TAudioClip* > TAudioClipManager::get_clip_list() const
{
	return m_clips;
}

bool TAudioClipManager::is_clip_in_selection(TAudioClip* clip)
{
	return m_clipselection.contains(clip);
}


/****************************** SLOTS ***************************/
/****************************************************************/


TCommand* TAudioClipManager::select_all_clips()
{
	PENTER;
	
    if (!m_clipselection.empty()) {
		return new ClipSelection(m_clips, this, "remove_from_selection", tr("Selection: Remove Clip"));
	}
		
	return new ClipSelection(m_clips, this, "add_to_selection", tr("Selection: Add Clip"));
}

TCommand* TAudioClipManager::invert_clip_selection()
{
	PENTER;
	
	return new ClipSelection(m_clips, this, "toggle_selected", tr("Selection: Invert"));
}

