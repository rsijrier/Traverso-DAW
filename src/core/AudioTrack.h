/*
Copyright (C) 2005-2019 Remon Sijrier

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


#ifndef AUDIO_TRACK_H
#define AUDIO_TRACK_H

#include <QString>
#include <QDomDocument>
#include <QList>

#include "ContextItem.h"
#include "Track.h"

#include "defines.h"

class Sheet;


class AudioTrack : public Track
{
        Q_OBJECT

public :
        AudioTrack(Sheet* sheet, const QString& name, int height);
        AudioTrack(Sheet* sheet, const QDomNode node);
        ~AudioTrack();

        AudioClip* init_recording();
        TCommand* add_clip(AudioClip* clip, bool historable=true, bool ismove=false);
        TCommand* remove_clip(AudioClip* clip, bool historable=true, bool ismove=false);
        AudioClip* get_clip_after(const TimeRef& pos);
        AudioClip* get_clip_before(const TimeRef& pos);
        Sheet* get_sheet() const {return m_sheet;}
        QDomNode get_state(QDomDocument doc, bool istemplate=false);
        QList<AudioClip*> get_audioclips() const {return  m_audioClips;}
        void get_render_range(TimeRef& startlocation, TimeRef& endlocation);
	bool show_clip_volume_automation() const {return m_showClipVolumeAutomation;}

        int set_state( const QDomNode& node );

        int arm();
        bool armed();
        int disarm();
        int process(nframes_t nframes);

protected:
        void add_input_bus(AudioBus* bus);

private :
        Sheet*          m_sheet;

        // only to be accessed/modified by AudioThread
        APILinkedList 	m_rtAudioClips;

        // only to be accessed from GUI thread
        QList<AudioClip*>   m_audioClips;

        int             m_numtakes{};
        bool            m_isArmed{};
	bool		m_showClipVolumeAutomation{};

        void set_armed(bool armed);
        void init();

signals:
        void audioClipAdded(AudioClip* clip);
        void audioClipRemoved(AudioClip* clip);

        void privateAudioClipAdded(AudioClip* clip);
        void privateAudioClipRemoved(AudioClip* clip);

        void armedChanged(bool isArmed);

public slots:
        void clip_position_changed(AudioClip* clip);

        TCommand* toggle_arm();
        TCommand* silence_others();
	TCommand* toggle_show_clip_volume_automation();

private slots:
        void private_add_clip(AudioClip* clip);
        void private_remove_clip(AudioClip* clip);
        void private_audioclip_added(AudioClip* clip);
        void private_audioclip_removed(AudioClip* clip);

        void private_clip_position_changed(AudioClip* clip);
};

#endif

