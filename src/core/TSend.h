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

#ifndef TSEND_H
#define TSEND_H

#include "APILinkedList.h"

#include <QDomElement>

class AudioBus;
class Track;

class TSend : public APILinkedListNode
{

public:
        TSend(Track* track);
        TSend(Track* track, AudioBus* bus);

        QDomNode get_state( QDomDocument doc);
        int set_state( const QDomNode& node );
        QString get_from_name() const ;

        enum {
                POSTSEND = 1,
                PRESEND = 2
        };

        AudioBus* get_bus() const {return m_bus;}
        QString get_name() const;
        qint64 get_id() const {return m_id;}
        qint64 get_bus_id() const;
        int get_type() const {return m_type;}

        bool is_smaller_then(APILinkedListNode* node) {return true;}

private:
        AudioBus*       m_bus;
        Track*          m_track;
        qint64          m_id;
        int             m_type;
};

#endif // TSEND_H
