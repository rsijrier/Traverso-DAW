/*
Copyright (C) 2010 Remon Sijrier

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

#include <QDomElement>
#include <QObject>

#include "TObjectIdTags.h"

class AudioBus;
class TTrack;

class TSend : public QObject
{
    Q_OBJECT

public:
    TSend(TTrack* track);
    // To a port (a hardware or software bus)
    TSend(TTrack* track, AudioBus* bus);
    // To a track: the target's process bus
    TSend(TTrack* track, TrackId target, AudioBus* targetBus);

    QDomNode get_state( QDomDocument doc);
    int set_state( const QDomNode& node );

    void set_type(int type) {m_type = type;}
    void set_gain(float gain);
    void set_pan(float pan);


    enum {
        POSTSEND = 1,
        PRESEND = 2,
        INPUT = 3
    };

    AudioBus* get_bus() const {return m_bus;}
    QString get_from_name() const ;
    // Given by TObjectFactory
    using IdTag = SendTag;
    SendId object_id() const
    {
        Q_ASSERT(m_objectId.is_valid());
        return m_objectId;
    }
    // The track this send goes to; invalid for a send to a port
    TrackId get_target_track() const {return m_targetTrack;}
    int get_type() const {return m_type;}
    float get_pan() const {return m_pan;}
    float get_gain() const {return m_gain;}

private:
    friend class TObjectFactory;
    SendId          m_objectId;
    AudioBus*       m_bus;
    TTrack*          m_track;
    TrackId         m_targetTrack;
    int             m_type{};
    float           m_gain{};
    float           m_pan{};

    void init();
};

