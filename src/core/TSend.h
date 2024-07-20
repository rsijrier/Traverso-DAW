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

#include <QDomElement>

class AudioBus;
class Track;

class TSend
{

public:
    TSend(Track* track);
    TSend(Track* track, AudioBus* bus);

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
    QString get_name() const;
    QString get_from_name() const ;
    qint64 get_id() const {return m_id;}
    qint64 get_bus_id() const;
    int get_type() const {return m_type;}
    float get_pan() const {return m_pan;}
    float get_gain() const {return m_gain;}

    bool operator<(const TSend& /*other*/) {
        return false;
    }

    TSend* next = nullptr;

private:
    AudioBus*       m_bus;
    Track*          m_track;
    qint64          m_id{};
    int             m_type{};
    float           m_gain{};
    float           m_pan{};

    void init();
};

#endif // TSEND_H
