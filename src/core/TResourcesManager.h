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

#pragma once

#include "TTimeRef.h"
#include <QString>
#include <QHash>

#include "TObjectIdTags.h"
#include <QList>
#include <QDomDocument>
#include <QObject>


class TBufferedAudioStream;
class TBufferedAudioStreamReader;
class TAudioClip;
class TProject;


// The role of a clip's new id in the resources window's items, read by a
// drop on the canvas; Qt::UserRole there is a source's old id
inline constexpr int ResourcesClipIdRole = Qt::UserRole + 1;

class TResourcesManager : public QObject
{
	Q_OBJECT
	
public:
    TResourcesManager(TProject* project);
    ~TResourcesManager();

	int set_state( const QDomNode& node );
	QDomNode get_state(QDomDocument doc);
	
	TBufferedAudioStreamReader* create_recording_source(const QString& dir,
				const QString& name,
				uint channelCount,
				qint64 sheetId);
	
	TBufferedAudioStreamReader* import_source(const QString& dir, const QString& name);
	TBufferedAudioStreamReader* get_silent_readsource();
	TAudioClip* new_audio_clip(const QString& name);
	TAudioClip* get_clip(ClipId id);
	// By old id, for the load of a track's clip list only (until the project
	// file references clips by their new id)
	TAudioClip* get_clip_for_old_id(qint64 oldId);

	void mark_clip_removed(TAudioClip* clip);
	void mark_clip_added(TAudioClip* clip);
    void set_source_for_clip(TAudioClip* clip, TBufferedAudioStreamReader *source);
	void destroy_clip(TAudioClip* clip);

public slots:
	void remove_source(TBufferedAudioStreamReader* source);

public:
	bool is_clip_in_use(ClipId id) const;
	bool is_source_in_use(qint64 id) const;

	TBufferedAudioStreamReader* get_readsource(qint64 id);
	
	QList<TBufferedAudioStreamReader*> get_all_audio_sources() const;
	QList<TAudioClip*> get_all_clips() const;

    // Takes of a recording that can never come back (TRecordingCommand):
    // their files are deleted or kept as the user prefers (Recording >
    // DiscardedTakes: ask, delete, keep). A take whose clip is in a track,
    // or whose source another clip uses, is left alone. Deleted takes leave
    // the project at once, their files when the project is saved without
    // them (delete_discarded_files()): a project file saved before still
    // names them, and stays right if the project closes unsaved.
    void discard_takes(const QList<TAudioClip*>& takes);
    // A take with no frames, out of its track: deleted without asking
    void drop_empty_take(TAudioClip* take);
    // After the project file was written; what could not be deleted is
    // tried again at the next save
    void delete_discarded_files();


private:
	struct ClipData {
		ClipData();
		TAudioClip* clip;
		bool inUse;
		bool isCopy;
		bool removed;
	};
	
	struct SourceData {
		SourceData();
		TBufferedAudioStreamReader* stream;
		int clipCount;
	};
	
	void delete_takes(const QList<TAudioClip*>& takes);

	TProject* m_project;
	QHash<qint64, SourceData* >	m_sources;
	QHash<ClipId, ClipData* >	m_clips;
	// The loaded clips by the old id the file names them by, for
	// get_clip_for_old_id() during the load
	QHash<qint64, ClipId>		m_clipOfOldId;
    // Taken out of the project by discard_takes(), freed with the project:
    // a take may have just left its track, and the audio thread lets go of
    // it only in a later cycle; once the project is off the audio device
    // nothing uses it
    QList<ClipData*> m_retiredClips;
    QList<SourceData*> m_retiredSources;
    QStringList m_discardedFiles;
	TBufferedAudioStreamReader*			m_silentReadSource;
	
	
signals:
	void stateRestored();
	void clipRemoved(TAudioClip* clip);
	void clipAdded(TAudioClip* clip);
	void sourceAdded(TBufferedAudioStreamReader* source);
	void sourceRemoved(TBufferedAudioStreamReader* source);
};




