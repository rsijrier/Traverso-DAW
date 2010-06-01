/*
Copyright (C) 2005-2010 Remon Sijrier

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

#include "Track.h"

#include "AudioChannel.h"
#include "PluginChain.h"
#include "Sheet.h"
#include "Utils.h"

#include "Debugger.h"

Track::Track(Sheet *sheet)
        : ProcessingData(sheet)
{
        m_sortIndex = -1;
        m_isSolo = m_mutedBySolo = m_isMuted = false;
        for (int i=0; i<2; ++i) {
                m_vumonitors.append(new VUMonitor());
        }
}

Track::~Track()
{
        // FIXME, we delete ourselves, but audiodevice could still be
        // monitoring our monitors!!!!
//        for (int i=0; i<2; ++i) {
//                delete m_vumonitors.at(i);
//        }
}


void Track::get_state( QDomElement& node, bool istemplate)
{
        if (! istemplate ) {
                node.setAttribute("id", m_id);
        } else {
                node.setAttribute("id", create_id());
        }
        node.setAttribute("name", m_name);
        node.setAttribute("pan", m_pan);
        node.setAttribute("mute", m_isMuted);
        node.setAttribute("solo", m_isSolo);
        node.setAttribute("mutedbysolo", m_mutedBySolo);
        node.setAttribute("height", m_height);
        node.setAttribute("sortindex", m_sortIndex);
        node.setAttribute("OutputBus", m_busOutName);

        QDomNode pluginChainNode = node.toDocument().createElement("PluginChain");
        pluginChainNode.appendChild(m_pluginChain->get_state(node.toDocument()));
        node.appendChild(pluginChainNode);
}


int Track::set_state( const QDomNode & node )
{
        QDomElement e = node.toElement();

        set_height(e.attribute( "height", "160" ).toInt() );
        m_sortIndex = e.attribute( "sortindex", "-1" ).toInt();
        m_name = e.attribute( "name", "" );
        set_muted(e.attribute( "mute", "" ).toInt());
        if (e.attribute( "solo", "" ).toInt()) {
                solo();
        }
        set_muted_by_solo(e.attribute( "mutedbysolo", "0").toInt());
        set_pan( e.attribute( "pan", "" ).toFloat() );
        m_id = e.attribute("id", "0").toLongLong();
        if (m_id == 0) {
                m_id = create_id();
        }

        QDomNode m_pluginChainNode = node.firstChildElement("PluginChain");
        if (!m_pluginChainNode.isNull()) {
                m_pluginChain->set_state(m_pluginChainNode);
        }

        return 1;
}


Command* Track::solo(  )
{
        // Not all Tracks have a sheet (e.g. Project Master)
        if (!m_sheet) {
                return 0;
        }

        m_sheet->solo_track(this);
        return (Command*) 0;
}



bool Track::is_solo()
{
        return m_isSolo;
}

bool Track::is_muted_by_solo()
{
        return m_mutedBySolo;
}


void Track::set_height(int h)
{
        m_height = h;
}


void Track::set_muted_by_solo(bool muted)
{
        PENTER;
        m_mutedBySolo = muted;
        emit audibleStateChanged();
}

void Track::set_solo(bool solo)
{
        m_isSolo = solo;
        if (solo)
                m_mutedBySolo = false;
        emit soloChanged(m_isSolo);
        emit audibleStateChanged();
}

void Track::set_sort_index( int index )
{
        m_sortIndex = index;
}

int Track::get_sort_index( ) const
{
        return m_sortIndex;
}
