/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TAudioClipFactory.h"

#include "TAudioClip.h"
#include "TAudioTrack.h"
#include "TBufferedAudioStreamReader.h"
#include "TProjectManager.h"
#include "TResourcesManager.h"
#include "TSheet.h"

#include <QCoreApplication>

TAudioClip* TAudioClipFactory::create_from_file(TAudioTrack* track,
                                                const QString& fileName,
                                                const TTimeRef& location,
                                                QString* error)
{
    Q_ASSERT(track);

    int splitpoint = fileName.lastIndexOf("/") + 1;
    QString dir = fileName.left(splitpoint - 1) + "/";
    QString name = fileName.right(fileName.length() - splitpoint);

    TBufferedAudioStreamReader* source = resources_manager()->import_source(dir, name);
    if (!source) {
        if (error) {
            *error = QCoreApplication::translate("TAudioClipFactory", "Can't import audio file %1").arg(fileName);
        }
        return nullptr;
    }

    return create_for_source(track, source, name, location);
}

TAudioClip* TAudioClipFactory::create_silent(TAudioTrack* track, const TTimeRef& location, const TTimeRef& length)
{
    return create_for_source(track,
                             resources_manager()->get_silent_readsource(),
                             QCoreApplication::translate("TAudioClipFactory", "Silence"),
                             location,
                             length);
}

TAudioClip* TAudioClipFactory::create_for_source(TAudioTrack* track,
                                                 TBufferedAudioStreamReader* source,
                                                 const QString& name,
                                                 const TTimeRef& location,
                                                 const TTimeRef& length)
{
    Q_ASSERT(track);
    Q_ASSERT(source);

    TAudioClip* clip = resources_manager()->new_audio_clip(name);
    resources_manager()->set_source_for_clip(clip, source);
    clip->set_sheet(track->get_sheet());
    clip->set_track(track);
    clip->move_to_location(location);

    if (length > TTimeRef()) {
        clip->set_right_edge(location + length);
    }

    return clip;
}
