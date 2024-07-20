/*
Copyright (C) 2008 Remon Sijrier 

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

#include "AudioClipGroup.h"

#include "AudioClip.h"
#include "AudioClipManager.h"
#include "TCommand.h"
#include "ProjectManager.h"
#include "ResourcesManager.h"
#include "Sheet.h"
#include "AudioTrack.h"

#include "Debugger.h"

AudioClipGroup::AudioClipGroup()
{
    m_location = new TLocation();
}

AudioClipGroup::AudioClipGroup(QList< AudioClip * > clips)
{
    m_location = new TLocation();
    m_clips = clips;
    update_state();
}

void AudioClipGroup::add_clip(AudioClip * clip)
{
    m_clips.append(clip);
    update_state();
}

void AudioClipGroup::set_clips(QList< AudioClip * > clips)
{
    m_clips = clips;
    update_state();
}

void AudioClipGroup::move_to(int trackIndex, TTimeRef location)
{
    PENTER;
    int trackIndexDelta = trackIndex - m_topTrackIndex;

    foreach(AudioClip* clip, m_clips) {
        if (trackIndexDelta != 0) {
                        AudioTrack* track = clip->get_sheet()->get_audio_track_for_index(clip->get_track()->get_sort_index() + trackIndexDelta);
            if (track) {
                                // Remove has to be done BEFORE adding, else the TRealTimeLinkedList logic
                                // gets messed up for the Tracks AudioClipList, which is an TRealTimeLinkedList :(
                TCommand::process_command(clip->get_track()->remove_clip(clip, false, true));
                TCommand::process_command(track->add_clip(clip, false, true));
            }
        }

        TTimeRef offset = clip->get_location()->get_start() - m_location->get_start();
        clip->set_location_start(location + offset);
    }

    update_state();
}

void AudioClipGroup::update_state()
{
    if (m_clips.isEmpty()) {
        return;
    }

    m_location->set_start(TTimeRef::max_length());
    m_location->set_end(TTimeRef());

    m_topTrackIndex = INT_MAX;
    m_bottomTrackIndex = 0;

    foreach(AudioClip* clip, m_clips) {
        int index = clip->get_track()->get_sort_index();
        if (index < m_topTrackIndex) {
            m_topTrackIndex = index;
        }
        if (index > m_bottomTrackIndex) {
            m_bottomTrackIndex = index;
        }
        if (m_location->get_start() > clip->get_location()->get_start()) {
            m_location->set_start(clip->get_location()->get_start());
        }
        if (m_location->get_end() < clip->get_location()->get_end()) {
            m_location->set_end(clip->get_location()->get_end());
        }
    }
}

void AudioClipGroup::set_snappable(bool snap)
{
    foreach(AudioClip* clip, m_clips) {
        clip->get_location()->set_snappable(snap);
    }
}

void AudioClipGroup::set_as_moving(bool move)
{
    foreach(AudioClip* clip, m_clips) {
        clip->set_as_moving(move);
    }
}

QList<AudioClip*> AudioClipGroup::copy_clips()
{
    QList<AudioClip*> newclips;

    foreach(AudioClip* clip, m_clips) {
        AudioClip* newclip = resources_manager()->get_clip(clip->get_id());
        newclip->set_sheet(clip->get_sheet());
        newclip->set_track(clip->get_track());
        newclip->set_location_start(clip->get_location()->get_start());
        newclips.append(newclip);
    }

    return newclips;
}

void AudioClipGroup::add_all_clips_to_tracks()
{
    foreach(AudioClip* clip, m_clips) {
        TCommand::process_command(clip->get_track()->add_clip(clip, false));
    }
}

void AudioClipGroup::remove_all_clips_from_tracks()
{
    foreach(AudioClip* clip, m_clips) {
        TCommand::process_command(clip->get_track()->remove_clip(clip, false));
    }
}

int AudioClipGroup::check_valid_track_index_delta(int delta)
{
        if (m_clips.isEmpty()) {
        return 0;
    }

    int allowedDeltaPlus = (m_clips.first()->get_sheet()->get_audio_track_count() - 1) - m_bottomTrackIndex;
    int allowedDeltaMin  = -m_topTrackIndex;

    if (delta > allowedDeltaPlus) {
        return allowedDeltaPlus;
    }

    if (delta < allowedDeltaMin) {
        return allowedDeltaMin;
    }

    return 0;
}

bool AudioClipGroup::is_locked() const
{
    foreach(AudioClip* clip, m_clips) {
        if (clip->is_locked()) {
            return true;
        }
    }
    return false;
}

