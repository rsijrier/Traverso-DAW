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

#include "TResourcesManager.h"
#include "TBufferedAudioStreamReader.h"
#include "TInformUser.h"
#include "TAudioClip.h"
#include "TObjectFactory.h"
#include "TProject.h"
#include "TSheet.h"
#include "Utils.h"
#include "TAudioDevice.h"
#include "TConfig.h"
#include "TLog.h"
#include "TProjectManager.h"
#include "TWaveform.h"

#include <QFile>



#include "Debugger.h"

/**	\class TResourcesManager
	\brief A class used to load / save the state of, create and delete ReadSources and AudioClips
 
 */

TResourcesManager::TResourcesManager(TProject* project)
	: QObject(project)
	, m_project(project)
    , m_silentReadSource(nullptr)
{
	PENTERCONS;
}


TResourcesManager::~TResourcesManager()
{
	PENTERDES;
    for(SourceData* data : std::as_const(m_sources)) {
        if (data->clipCount == 0) {
            TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "Unused source file: %s", QS_C(data->stream->get_dir()));
        }
        if (! data->stream->ref()) {
            delete data->stream;
		}
		delete data;
	}

    // As above, and before any clip goes: a take's reader was referenced at
    // its creation, and its clip's destructor takes it out of the read
    // scheduler and the sheet deletes it (then it may be gone already)
    for (SourceData* data : std::as_const(m_retiredSources)) {
        if (!data->stream->ref()) {
            delete data->stream;
        }
        delete data;
    }

    for(ClipData* data : std::as_const(m_clips)) {
		delete data->clip;
		delete data;
	}

    for (ClipData* data : std::as_const(m_retiredClips)) {
        delete data->clip;
        delete data;
    }
}


void TResourcesManager::discard_takes(const QList<TAudioClip*>& takes)
{
    // Only takes that are out of every track and whose source nothing uses
    QList<TAudioClip*> discarded;
    QStringList names;
    for (TAudioClip* take : takes) {
        ClipData* clip = m_clips.value(take->get_id());
        SourceData* source = m_sources.value(take->get_readsource_id());
        if (!clip || !clip->removed || !source || source->clipCount > 0) {
            TLOG_I("recording", "keeping take {}: still in use", take->get_name().toStdString());
            continue;
        }
        discarded.append(take);
        names.append(source->stream->get_name());
    }
    if (discarded.isEmpty()) {
        return;
    }

    const QString behaviour = config().get_property("Recording", "DiscardedTakes", "ask").toString();
    bool remove = behaviour == "delete";
    if (behaviour == "ask") {
        remove = pm().ask_discard(tr("%n recorded take(s) were undone and can not be brought back anymore:\n\n%1\n\n"
                                     "Delete their audio files when the project is saved?", "", int(discarded.size()))
                                  .arg(names.join(QLatin1Char('\n'))));
    }
    if (!remove) {
        TLOG_I("recording", "keeping the files of {} discarded take(s)", discarded.size());
        return;
    }
    delete_takes(discarded);
}

void TResourcesManager::drop_empty_take(TAudioClip* take)
{
    delete_takes({take});
}

void TResourcesManager::delete_takes(const QList<TAudioClip*>& takes)
{
    for (TAudioClip* take : takes) {
        ClipData* clip = m_clips.take(take->get_id());
        SourceData* source = m_sources.take(take->get_readsource_id());
        if (!clip || !source) {
            continue;
        }
        const QString audioPath = source->stream->get_filename();
        const QString peakPath = TWaveform::peak_path(source->stream);
        // Out of the project: neither is saved anymore. The objects stay
        // until the project closes (see m_retiredClips), the files until
        // it is saved.
        m_retiredClips.append(clip);
        m_retiredSources.append(source);
        emit sourceRemoved(source->stream);
        // The peak file only: a part file has a name of its writer's own and
        // is renamed, given up or evicted by the peak cache
        m_discardedFiles << audioPath << peakPath;
        TLOG_I("recording", "discarded take {}: {} goes when the project is saved", take->get_name().toStdString(),
               audioPath.toStdString());
    }
}

void TResourcesManager::delete_discarded_files()
{
    QStringList kept;
    for (const QString& path : std::as_const(m_discardedFiles)) {
        if (QFile::exists(path)) {
            const bool removed = QFile::remove(path);
            TLOG_I("recording", "{} {}", path.toStdString(), removed ? "deleted" : "could not be deleted, tried again at the next save");
            if (!removed) {
                kept << path;
            }
        }
    }
    m_discardedFiles = kept;
}

QDomNode TResourcesManager::get_state( QDomDocument doc )
{
	PENTER;

	QDomElement rsmNode = doc.createElement("ResourcesManager");
	
	QDomElement audioSourcesElement = doc.createElement("AudioSources");
	
	foreach(SourceData* data, m_sources) {
        TBufferedAudioStreamReader* source = data->stream;
		audioSourcesElement.appendChild(source->get_state(doc));
	}
	
	rsmNode.appendChild(audioSourcesElement);
	
	
	QDomElement audioClipsElement = doc.createElement("AudioClips");
	
	foreach(ClipData* data, m_clips) {
		TAudioClip* clip = data->clip;
		
		if (data->isCopy && data->removed) {
			continue;
		}
		
		// If the clip was 'getted', then it's state has been fully set
		// and likely changed, so we can get the 'new' state from it.
		if (data->inUse) {
			audioClipsElement.appendChild(clip->get_state(doc));
		} else {
			// In case it wasn't we should use the 'old' domNode which
			// was set during set_state(); with the id it got at the load, so
			// that it keeps it at the next one. A copy in this document: the
			// clip's own node stays untouched, its document may be gone
			QDomElement element = doc.importNode(clip->get_dom_node(), true).toElement();
			TObjectFactory::write(element, clip->object_id());
			audioClipsElement.appendChild(element);
		}
	}
	
	rsmNode.appendChild(audioClipsElement);
	
	return rsmNode;
}


int TResourcesManager::set_state( const QDomNode & node )
{
	QDomNode sourcesNode = node.firstChildElement("AudioSources").firstChild();
	
	while(!sourcesNode.isNull()) {
        TBufferedAudioStreamReader* stream = new TBufferedAudioStreamReader(sourcesNode);
		SourceData* data = new SourceData();
        data->stream = stream;
        m_sources.insert(stream->get_id(), data);
		sourcesNode = sourcesNode.nextSibling();
        if (stream->get_channel_count() == 0) {
            m_silentReadSource = stream;
		}
	}
	
	
	QDomNode clipsNode = node.firstChildElement("AudioClips").firstChild();
	
	while(!clipsNode.isNull()) {
		TAudioClip* clip = m_project->objects().load<TAudioClip>(clipsNode.toElement(), clipsNode);
		ClipData* data = new ClipData();
		data->clip = clip;
		m_clips.insert(clip->get_id(), data);
		clipsNode = clipsNode.nextSibling();
	}
	
	
	emit stateRestored();
	
	return 1;
}


TBufferedAudioStreamReader* TResourcesManager::import_source(const QString& dir, const QString& name)
{
	QString fileName = dir + name;
	foreach(SourceData* data, m_sources) {
        if (data->stream->get_filename() == fileName) {
            TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "id is %lld", data->stream->get_id());
            return get_readsource(data->stream->get_id());
		}
	}
	
    TBufferedAudioStreamReader* stream = new TBufferedAudioStreamReader(dir, name);
	SourceData* data = new SourceData();
    data->stream = stream;
        stream->set_created_by_sheet(m_project->get_current_session()->get_id());
	
    m_sources.insert(stream->get_id(), data);
	
    stream = get_readsource(stream->get_id());
	
    if (stream->get_error() < 0) {
        m_sources.remove(stream->get_id());
        delete stream;
        return nullptr;
	}
	
    emit sourceAdded(stream);

    return stream;
}


TBufferedAudioStreamReader* TResourcesManager::create_recording_source(
	const QString& dir,
	const QString& name,
    uint channelCount,
 	qint64 sheetId)
{
	PENTER;
	
    TBufferedAudioStreamReader* stream = new TBufferedAudioStreamReader(dir, name, channelCount);
	SourceData* data = new SourceData();
    data->stream = stream;
	
    stream->set_original_bit_depth(m_project->audio_device().get_bit_depth());
    stream->set_created_by_sheet(sheetId);
    stream->ref();
	
    m_sources.insert(stream->get_id(), data);
	
    emit sourceAdded(stream);
	
    return stream;
}


TBufferedAudioStreamReader* TResourcesManager::get_silent_readsource()
{
	if (!m_silentReadSource) {
        m_silentReadSource = new TBufferedAudioStreamReader();
		SourceData* data = new SourceData();
        data->stream = m_silentReadSource;
		m_sources.insert(m_silentReadSource->get_id(), data);
		m_silentReadSource->set_created_by_sheet( -1 );
	}
	
	m_silentReadSource = get_readsource(m_silentReadSource->get_id());
	
	return m_silentReadSource;
}


TBufferedAudioStreamReader * TResourcesManager::get_readsource(qint64 id)
{
	SourceData* data = m_sources.value(id);
	
	if (!data) {
        PWARN(QString("ResourcesManager::get_readsource(): ReadSource with id %1 is not in my database!").arg(id).toLatin1().data());
        return nullptr;
	}
	
    TBufferedAudioStreamReader* source = data->stream;
	
	// When the AudioSource is "get", do a ref counting.
	// If the source allready was ref counted, create a deep copy
	if (source->ref()) {
		PMESG("Creating deep copy of ReadSource: %s", QS_C(source->get_name()));
		source = source->deep_copy();
		source->ref();
	}
	
	if ( source->init() < 0) {
		tInformUser().warning( tr("ResourcesManager::  Failed to initialize ReadSource %1 (Reason: %2)")
                .arg(source->get_filename(), source->get_error_string()));
	}
	
	return source;
}

/**
 * 	Get the TAudioClip with id \a id

    This function will return nullptr if no TAudioClip was found with id \a id.
	
	Only ONE TAudioClip instance with this id can be retrieved via this function. 
	Using this function multiple times with the same id will implicitely create 
	a new TAudioClip with a new unique id!!

 * @param id 	The unique id of the TAudioClip to get
 * @return 	The TAudioClip with id \a id, 0 if no TAudioClip was found, and a 
		'deep copy' of the TAudioClip with id \a id if the TAudioClip was allready
		getted before via this function.
 */
TAudioClip* TResourcesManager::get_clip(qint64 id)
{
	ClipData* data = m_clips.value(id);
	 
    if (!data) {
        return nullptr;
	}
	
	TAudioClip* clip = data->clip;
	
	if (data->inUse && !data->removed) {
		PMESG("Creating deep copy of Clip %s", QS_C(clip->get_name()));
		clip = clip->create_copy();
		
		// It is NOT possible for 2 clips to have the same ID
		// check for this, since it's absolutely crucial, and 
		// indicates a design error somewhere ! (most likely in 
		// AudioClip::create_copy();
		Q_ASSERT( ! m_clips.contains(clip->get_id()) );
		ClipData* copy = new ClipData();
		copy->clip = clip;
		copy->isCopy = true;
		
		m_clips.insert(clip->get_id(), copy);
		
		// Now that we have created a copy of the audioclip, start
		// the usual get_clip routine again to properly init the clip
		// and other stuff.
		return get_clip(clip->get_id());
	}
	
    TBufferedAudioStreamReader* source = get_readsource(data->clip->get_readsource_id());
	clip->set_audio_source(source);
	
	data->inUse = true;
	
	return clip;
}

TAudioClip* TResourcesManager::new_audio_clip(const QString& name)
{
	PENTER;
	TAudioClip* clip = m_project->objects().make<TAudioClip>(name);
	ClipData* data = new ClipData();
	data->clip = clip;
	// In use from the start. Not through get_clip(): that sets the source a
	// loaded clip refers to, a new clip has none yet, the caller sets it
	data->inUse = true;
	m_clips.insert(clip->get_id(), data);
	return clip;
}

QList<TBufferedAudioStreamReader*> TResourcesManager::get_all_audio_sources( ) const
{
    QList< TBufferedAudioStreamReader * > list;
	foreach(SourceData* data, m_sources) {
        list.append(data->stream);
	}
	if (m_silentReadSource) {
		list.removeAll(m_silentReadSource);
	}
	return list;
}

QList< TAudioClip * > TResourcesManager::get_all_clips() const
{
	QList<TAudioClip* > list;
	foreach(ClipData* data, m_clips) {
		list.append(data->clip);
	}
	return list;
}


void TResourcesManager::mark_clip_removed(TAudioClip * clip)
{
	ClipData* data = m_clips.value(clip->get_id());
	if (!data) {
//		PERROR("Clip with id %lld is not in my database!", clip->get_id());
		return;
	}
	data->removed = true;
	
	SourceData* sourcedata = m_sources.value(clip->get_readsource_id());
	if (!sourcedata) {
//		PERROR("Source %lld not in database", clip->get_readsource_id());
	} else {
		sourcedata->clipCount--;
	}
	
	emit clipRemoved(clip);
}

void TResourcesManager::mark_clip_added(TAudioClip * clip)
{
	ClipData* clipdata = m_clips.value(clip->get_id());
	if (!clipdata) {
//		PERROR("Clip with id %lld is not in my database!", clip->get_id());
		return;
	}
	clipdata->removed = false;
	
	SourceData* sourcedata = m_sources.value(clip->get_readsource_id());
	if (!sourcedata) {
//		PERROR("Source %lld not in database", clip->get_readsource_id());
	} else {
		sourcedata->clipCount++;
	}
	
	emit clipAdded(clip);
}


bool TResourcesManager::is_clip_in_use(qint64 id) const
{
	ClipData* data = m_clips.value(id);
	if (!data) {
//		PERROR("Clip with id %lld is not in my database!", id);
		return false;
	}
	return data->inUse && !data->removed;
}

bool TResourcesManager::is_source_in_use(qint64 id) const
{
	SourceData* data = m_sources.value(id);
	if (!data) {
//		PERROR("Source with id %lld is not in my database!", id);
		return false;
	}
	
	return data->clipCount > 0 ? true : false;
}

void TResourcesManager::set_source_for_clip(TAudioClip * clip, TBufferedAudioStreamReader* source)
{
	clip->set_audio_source(source);
}

TResourcesManager::SourceData::SourceData()
{
    stream = nullptr;
	clipCount = 0;
}

TResourcesManager::ClipData::ClipData()
{
    clip = nullptr;
	inUse = false;
	isCopy = false;
	removed = false;
}

void TResourcesManager::destroy_clip(TAudioClip * clip)
{
	ClipData* data = m_clips.value(clip->get_id());
	if (!data) {
//		PERROR("Clip with id %lld not in database", clip->get_id());
	} else {
		m_clips.remove(clip->get_id());
		delete data;
		delete clip;
	}
	
}

void TResourcesManager::remove_source(TBufferedAudioStreamReader * source)
{
	SourceData* data = m_sources.value(source->get_id());
	if (!data) {
	} else {
		if (data->clipCount > 0) {
			tInformUser().critical(tr("ResourcesManager: Received request to remove Audio Source %1"
				"but it is still in use by %2 AudioClips!!. NOT removing it!").
				arg(source->get_name()).arg(data->clipCount));
			return;
		}
		
		m_sources.remove(source->get_id());

		emit sourceRemoved(source);

		delete data;
		delete source;
	}
}

