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

#include "TGain.h"
#include "TSend.h"
#include "TObjectFactory.h"
#include "Debugger.h"

#include "AudioBus.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TTrack.h"


TSend::TSend(TTrack* track)
    : QObject(track)
    , m_track(track)
{
        m_type = POSTSEND;
        m_bus = nullptr;
        init();
}

TSend::TSend(TTrack* track, AudioBus* bus)
    : QObject(track)
    , m_track(track)
{
        m_bus = bus;
        init();
}

TSend::TSend(TTrack* track, TrackId target, AudioBus* targetBus)
    : QObject(track)
    , m_track(track)
    , m_targetTrack(target)
{
        m_bus = targetBus;
        init();
}

void TSend::init()
{
        m_type = POSTSEND;
        m_gain = 1.0;
        m_pan = 0.0;
}

QDomNode TSend::get_state( QDomDocument doc)
{
        QDomElement node = doc.createElement("Send");

        TObjectFactory::write(node, object_id());
        TObjectFactory::write_owner(node, m_track->object_id());
        node.setAttribute("gain", m_gain);
        node.setAttribute("pan", m_pan);
        if (m_targetTrack.is_valid()) {
                TObjectFactory::write_reference(node, "targettrack", m_targetTrack);
        } else if (m_bus) {
                node.setAttribute("targetport", m_bus->get_name());
        }
        if (m_bus) {
                node.setAttribute("busname", m_bus->get_name());
        }
        if (m_type == POSTSEND) {
                node.setAttribute("type", QString("post"));
        } else if (m_type == PRESEND) {
                node.setAttribute("type", QString("pre"));
        } else if (m_type == INPUT) {
                node.setAttribute("type", QString("input"));
        }

        return node;
}


int TSend::set_state( const QDomNode & node )
{
        TProject* project = pm().get_project();
        if (!project) {
                TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "TSend::set_state: Oh boy, no project?? Can't restore state without a project running!!");
                return -1;
        }

        QDomElement e = node.toElement();

        QString type = e.attribute("type", "");
        QString busName = e.attribute("busname", "No Busname in Project file");
        set_gain(e.attribute("gain", "1.0").toFloat());
        set_pan(e.attribute("pan", "0.00").toFloat());

        if (type == "post") {
                m_type = POSTSEND;
        } else if (type == "pre") {
                m_type = PRESEND;
        } else {
                // default to post send if no type was stored
                m_type = POSTSEND;
        }


        // Exactly one target: a track, or a port by name
        const std::optional<TrackId> target = TObjectFactory::read_reference<TrackTag>(e, "targettrack");
        const bool hasPort = e.hasAttribute("targetport");
        if (target.has_value() == hasPort) {
                TLOG_W("track", "a send of {} names {} target: not loaded", m_track->get_name().toStdString(),
                       hasPort ? "two kinds of" : "no");
                return -1;
        }

        if (target) {
                m_targetTrack = *target;
                m_bus = project->get_audio_bus(m_targetTrack);
        } else {
                m_bus = project->get_playback_bus(e.attribute("targetport"));
        }

        if (!m_bus) {
                TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "TSend::set_state: Project didn't return my Bus (%s)!", busName.toLatin1().data());
                return -1;
        }


        return 1;
}

QString TSend::get_from_name() const
{
        return m_track->get_name();
}

void TSend::set_pan(float pan)
{
        if ( pan < -1.0 ) {
                m_pan=-1.0;
        } else if ( pan > 1.0 ) {
                m_pan=1.0;
        } else {
                m_pan=pan;
        }
}

void TSend::set_gain(float gain)
{
    m_gain = TGain::clamp(gain);
}
