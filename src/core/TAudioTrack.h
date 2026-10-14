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


#ifndef TAUDIO_TRACK_H
#define TAUDIO_TRACK_H

#include <QString>
#include <QDomDocument>
#include <QList>

#include "TContextItem.h"
#include "TProcessCallBackData.h"
#include "TRtList.h"
#include "TTimeRef.h"
#include "TTrack.h"


class TSheet;



class TAudioTrack : public TTrack
{
    Q_OBJECT

public :
    TAudioTrack(TSheet* sheet, const QString& name, int height);
    TAudioTrack(TSheet* sheet, const QDomNode node);
    ~TAudioTrack();

    TAudioClip* init_recording();
    void apply_clip_mutation(const QList<TAudioClip*>& clipsToAdd, const QList<TAudioClip*>& clipsToRemove);

    TAudioClip* get_clip_after(const TTimeRef& pos) const;
    TAudioClip* get_audio_clip_after(TAudioClip* audioClip) const;
    TAudioClip* get_clip_before(const TTimeRef& pos) const;
    TAudioClip* get_audio_clip_before(TAudioClip* audioClip) const;
    TAudioClip* get_clip_at_location(const TTimeRef& location) const;

    TSheet* get_sheet() const {return m_sheet;}
    QDomNode get_state(QDomDocument doc, bool istemplate=false);
    QList<TAudioClip*> get_audioclips() const {return m_clips.items();}

    // Return the rightmost AudioClip track end location
    TTimeRef get_end_location() const;

    bool get_export_range(TTimeRef& trackExportStartLocation, TTimeRef& trackExportEndLocation);
    bool show_clip_volume_automation() const {return m_showClipVolumeAutomation;}

    int set_state( const QDomNode& node );

    int arm();
    bool armed();
    int disarm();
    void toggle_show_clip_volume_automation();

    int process(TProcessCallBackData &processData);

    bool operator<(const TAudioTrack &other) {
        printf("bool operator<(const AudioTrack &other)\n");
        return this->get_sort_index() < other.get_sort_index();
    }


    TAudioTrack* next = nullptr;

    void clip_position_changed(TAudioClip* clip);


protected:
    void add_input_bus(AudioBus* bus);

private:
    TSheet*          m_sheet;

    // Sorted by start location. The GUI thread owns the list,
    // process() reads its snapshot in the audio thread.
    TRtList<TAudioClip>  m_clips;

    int             m_numtakes{};
    bool            m_isArmed{};
    bool		m_showClipVolumeAutomation{};

    void set_armed(bool armed);
    void init();

public slots:
    TCommand* toggle_arm();

signals:
    void audioClipAdded(TAudioClip* clip);
    void audioClipRemoved(TAudioClip* clip);

    void armedChanged(bool isArmed);
};

class TBounceTrack : public TAudioTrack
{
    Q_OBJECT

public:
    TBounceTrack(TSheet* sheet, const QString& name, int height);
    ~TBounceTrack();


};

#endif

