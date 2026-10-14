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

#include "TClipsViewPort.h"

#include "TAudioClip.h"
#include "TAudioTrack.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TBufferedAudioStreamReader.h"
#include "TResourcesManager.h"
#include "TSheetWidget.h"
#include "TSheetView.h"
#include "TSheet.h"
#include "TAudioTrackView.h"
#include "TViewItem.h"
#include "TAudioClipFactory.h"
#include "decode/AbstractAudioReader.h"
#include "TInformUser.h"
#include "TInputEventDispatcher.h"

#include <QScrollBar>
#include <QSet>
#include <QPaintEngine>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QDragEnterEvent>
#include <QMimeData>
		
#include <Debugger.h>
		
TClipsViewPort::TClipsViewPort(QGraphicsScene* scene, TSheetWidget* sw)
	: TViewPort(scene, sw)
{
	m_sw = sw;
	viewport()->setAttribute(Qt::WA_OpaquePaintEvent);

	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	scale(1.0, 1.0);
}

void TClipsViewPort::resizeEvent( QResizeEvent * e )
{
	TViewPort::resizeEvent(e);
	m_sw->get_sheetview()->clipviewport_resize_event();
}


void TClipsViewPort::paintEvent(QPaintEvent * e)
{
	QGraphicsView::paintEvent(e);
}


void TClipsViewPort::dragEnterEvent( QDragEnterEvent * event )
{
	m_importFiles.clear();
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
			
			// Only probe the file here, it is imported on drop. If a file
			// can't be decoded, the D&D is marked as failed.
			if (!AbstractAudioReader::can_decode(fileName)) {
				m_importFiles.clear();
				break;
			}

			m_importFiles.append(fileName);
		}
	}
	
    if (!m_importFiles.empty() || !m_resourcesImport.empty()) {
        event->accept();
    }
}

void TClipsViewPort::dropEvent(QDropEvent* event )
{
	PENTER;
	Q_UNUSED(event)
	
	if (!m_importTrack) {
		return;
	}

	QList<TAudioClip*> clips;

	TTimeRef startpos = TTimeRef(mapFromGlobal(QCursor::pos()).x() * m_sw->get_sheetview()->timeref_scalefactor);
	
	foreach(qint64 id, m_resourcesImport) {
		TAudioClip* clip = resources_manager()->get_clip(id);
		if (clip) {
			bool hadSheet = clip->has_sheet();
            clip->set_sheet(m_sw->get_sheet());
			clip->set_track(m_importTrack);
			if (!hadSheet) {
				clip->set_state(clip->get_dom_node());
			}
            clip->move_to_location(startpos);
            startpos = clip->get_location_end();
            clips.append(clip);
            continue;
		}
		TBufferedAudioStreamReader* source = resources_manager()->get_readsource(id);
		if (source) {
			clip = resources_manager()->new_audio_clip(source->get_short_name());
            resources_manager()->set_source_for_clip(clip, source);
			clip->set_sheet(m_importTrack->get_sheet());
			clip->set_track(m_importTrack);
			clip->move_to_location(startpos);
            startpos = clip->get_location_end();
            clips.append(clip);
        }
	}
	
	// Place the first file at the cursor, the others at the end of the track,
	// including the clips added by this drop
	TTimeRef trackEnd = m_importTrack->get_end_location();
	for (TAudioClip* clip : std::as_const(clips)) {
		trackEnd = std::max(trackEnd, clip->get_location_end());
	}

	bool firstItem = true;
	for (const QString& fileName : std::as_const(m_importFiles)) {
		QString error;
		TTimeRef location = firstItem ? startpos : trackEnd;
		TAudioClip* clip = TAudioClipFactory::create_from_file(m_importTrack, fileName, location, &error);
		if (!clip) {
			tInformUser().warning(error);
			continue;
		}
		firstItem = false;
		trackEnd = std::max(trackEnd, clip->get_location_end());
		clips.append(clip);
	}

	if (clips.isEmpty()) {
		return;
	}

	// All dropped clips in one undo step
	TSheet* sheet = m_sw->get_sheet();
	QUndoCommand* command = sheet->create_command_mutate_clips(clips, {}, tr("Import %n audiofile(s)", "", clips.size()));

	if (QUndoStack* undoStack = sheet->get_history_stack()) {
		undoStack->push(command);
	} else {
		command->redo();
		delete command;
	}
}

void TClipsViewPort::dragMoveEvent( QDragMoveEvent * event )
{
	TProject* project = pm().get_project();
	if (!project) {
        event->ignore();
		return;
	}
	
    TSheet* sheet = qobject_cast<TSheet*>(project->get_current_session());

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
		TAudioTrackView* tv = dynamic_cast<TAudioTrackView*>(obj);
		if (tv) {
			m_importTrack = tv->get_track();
            event->accept();
			return;
        }
	}
    event->ignore();
}
