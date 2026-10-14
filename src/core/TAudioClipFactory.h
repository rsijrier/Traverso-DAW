/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TTimeRef.h"
#include <QString>

class TAudioClip;
class TAudioTrack;
class TBufferedAudioStreamReader;

/**
 * @brief Creates audio clips for a track, without adding them to it.
 *
 * Add the created clips with TSheet::create_command_mutate_clips(), so that
 * adding them is undoable and reaches the audio thread as one change.
 */
class TAudioClipFactory
{
public:
    // Creates a clip for an audio file at location. Returns nullptr and sets
    // error if the file can't be imported.
    static TAudioClip* create_from_file(TAudioTrack* track,
                                        const QString& fileName,
                                        const TTimeRef& location,
                                        QString* error = nullptr);

    // Creates a silent clip of the given length at location
    static TAudioClip* create_silent(TAudioTrack* track, const TTimeRef& location, const TTimeRef& length);

    // Creates a clip for an existing read source at location. A length of zero
    // means the full length of the source.
    static TAudioClip* create_for_source(TAudioTrack* track,
                                         TBufferedAudioStreamReader* source,
                                         const QString& name,
                                         const TTimeRef& location,
                                         const TTimeRef& length = TTimeRef());
};
