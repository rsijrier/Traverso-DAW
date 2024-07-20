/*
Copyright (C) 2006-2007 Remon Sijrier 

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

#include "ClipsViewPort.h"

#include "AudioTrack.h"
#include "Project.h"
#include "ProjectManager.h"
#include "ReadSource.h"
#include "ResourcesManager.h"
#include "SheetWidget.h"
#include "SheetView.h"
#include "Sheet.h"
#include "AudioTrackView.h"
#include "ViewItem.h"
#include "TAudioFileImportCommand.h"
#include "CommandGroup.h"
#include "RemoveClip.h"


#include <QScrollBar>
#include <QSet>
#include <QPaintEngine>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QDragEnterEvent>
#include <QMimeData>
		
#include <Debugger.h>
		
ClipsViewPort::ClipsViewPort(QGraphicsScene* scene, SheetWidget* sw)
	: ViewPort(scene, sw)
{
	m_sw = sw;
	viewport()->setAttribute(Qt::WA_OpaquePaintEvent);

	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	scale(1.0, 1.0);
}

void ClipsViewPort::resizeEvent( QResizeEvent * e )
{
	ViewPort::resizeEvent(e);
//	m_sw->get_sheetview()->clipviewport_resize_event();
}


void ClipsViewPort::paintEvent(QPaintEvent * e)
{
	QGraphicsView::paintEvent(e);
}


void ClipsViewPort::dragEnterEvent( QDragEnterEvent * event )
{
	m_imports.clear();
	m_resourcesImport.clear();
	
	// let's see if the D&D was from the resources bin.
	if (event->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
		QByteArray encodedData = event->mimeData()->data("application/x-qabstractitemmodeldatalist");
		QDataStream stream(&encodedData, QIODevice::ReadOnly);
		int r, c;
		QMap<int, QVariant> v;
		
		while (!stream.atEnd()) {
			stream >> r >> c >> v;
			qint64 id = v.value(Qt::UserRole).toLongLong();
			if (!id) {
				continue;
			}
			m_resourcesImport.append(id);
		}
	}
	
	// and who knows, it could have been a D&D drop from a filemanager...
    if (event->mimeData()->hasUrls()) {

		foreach(QUrl url, event->mimeData()->urls()) {
			QString fileName = url.toLocalFile();
			
			if (fileName.isEmpty()) {
                continue;
			}
			
            TAudioFileImportCommand* import = new TAudioFileImportCommand(pm().get_project());
            import->set_file_name(fileName);
			m_imports.append(import);
			
			// If a readsource fails to init, the D&D should be
			// marked as failed, cleanup allready created imports,
			// and clear the import list.
            if (import->create_readsource() == -1) {
                foreach(TAudioFileImportCommand* import, m_imports) {
					delete import;
				}
				m_imports.clear();
				break;
			}
		}
	}
	
    if (!m_imports.empty() || !m_resourcesImport.empty()) {
        event->accept();
    }
}

void ClipsViewPort::dropEvent(QDropEvent* event )
{
	PENTER;
	Q_UNUSED(event)
	
	if (!m_importTrack) {
		return;
	}

	CommandGroup* group = new CommandGroup(m_sw->get_sheet(), 
               tr("Import %n audiofile(s)", "", m_imports.size() + m_resourcesImport.size()));
	
	TTimeRef startpos = TTimeRef(mapFromGlobal(QCursor::pos()).x() * m_sw->get_sheetview()->timeref_scalefactor);
	
	foreach(qint64 id, m_resourcesImport) {
		AudioClip* clip = resources_manager()->get_clip(id);
		if (clip) {
			bool hadSheet = clip->has_sheet();
            clip->set_sheet(m_sw->get_sheet());
			clip->set_track(m_importTrack);
			if (!hadSheet) {
				clip->set_state(clip->get_dom_node());
			}
			clip->set_location_start(startpos);
            startpos = clip->get_location()->get_end();
			AddRemoveClip* arc = new AddRemoveClip(clip, AddRemoveClip::ADD);
			group->add_command(arc);
			continue;
		}
		ReadSource* source = resources_manager()->get_readsource(id);
		if (source) {
			clip = resources_manager()->new_audio_clip(source->get_short_name());
			resources_manager()->set_source_for_clip(clip, source);
			clip->set_sheet(m_importTrack->get_sheet());
			clip->set_track(m_importTrack);
			clip->set_location_start(startpos);
            startpos = clip->get_location()->get_end();
			AddRemoveClip* arc = new AddRemoveClip(clip, AddRemoveClip::ADD);
			group->add_command(arc);
		}
	}
	
	bool firstItem = true;
    foreach(TAudioFileImportCommand* import, m_imports) {
		import->set_track(m_importTrack);
		if (firstItem) {
			// Place first item at cursor, others at end of track.
			import->set_import_location(startpos);
			firstItem = false;
		}
		group->add_command(import);
	}

	TCommand::process_command(group);
}

void ClipsViewPort::dragMoveEvent( QDragMoveEvent * event )
{
	Project* project = pm().get_project();
	if (!project) {
        event->ignore();
		return;
	}
	
    Sheet* sheet = qobject_cast<Sheet*>(project->get_current_session());

	if (!sheet) {
        event->ignore();
		return;
	}
	
	m_importTrack = nullptr;
	
	// hmmm, code below is candidate for improvements...?
	
	// no mouse move events during D&D move events...
	// So we need to calculate the scene pos ourselves.
    QList<QGraphicsItem *> itemlist = items(mapFromGlobal(QCursor::pos()));
	foreach(QGraphicsItem* obj, itemlist) {
		AudioTrackView* tv = dynamic_cast<AudioTrackView*>(obj);
		if (tv) {
			m_importTrack = tv->get_track();
            event->accept();
			return;
        }
	}
    event->ignore();
}

