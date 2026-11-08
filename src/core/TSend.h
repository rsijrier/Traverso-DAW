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
    TSend(TTrack* track, AudioBus* bus);

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
    qint64 get_id() const {return m_id;}
    // Given by TObjectFactory
    using IdTag = SendTag;
    SendId object_id() const
    {
        Q_ASSERT(m_objectId.is_valid());
        return m_objectId;
    }
    QObject* id_owner() const;
    qint64 get_bus_id() const;
    int get_type() const {return m_type;}
    float get_pan() const {return m_pan;}
    float get_gain() const {return m_gain;}

private:
    friend class TObjectFactory;
    SendId          m_objectId;
    AudioBus*       m_bus;
    TTrack*          m_track;
    qint64          m_id{};
    int             m_type{};
    float           m_gain{};
    float           m_pan{};

    void init();
};

