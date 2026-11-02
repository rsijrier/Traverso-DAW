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

#include "TBusTrack.h"

#include "AudioBus.h"
#include "TAudioPluginChain.h"
#include "Utils.h"
#include "TSession.h"
#include "TAudioDevice.h"
#include "TFloatCompare.h"

TBusTrack::TBusTrack(TSession* session, const QString& name, int channelCount)
        : TTrack(session)
{
        QObject::tr("TBusTrack");

        m_name = name;
        m_channelCount = channelCount;

        create_process_bus();

        m_processBus->set_id(get_id());
        m_processBus->set_name(m_name);

        session->set_track_height(get_id(), INITIAL_HEIGHT);
}

TBusTrack::TBusTrack(TSession *session, QDomNode /*node*/)
        : TTrack(session)
{
}

TBusTrack::~TBusTrack()
{
        delete m_processBus;
}

QDomNode TBusTrack::get_state( QDomDocument doc, bool istemplate)
{
        QDomElement node = doc.createElement("BusTrack");
        TTrack::get_state(doc, node, istemplate);

        node.setAttribute("channelcount", m_channelCount);

        return node;
}

int TBusTrack::set_state( const QDomNode & node )
{
        QDomElement e = node.toElement();

        bool ok;
        m_channelCount = e.attribute("channelcount", "2").toInt(&ok);

        // The process bus has the track's id: sends find a bus track by it,
        // and TTrack::set_state() restores the sends, so the file's id is on
        // the track and on its bus before that (T64's loop check relies on it)
        set_id(e.attribute("id", "0").toLongLong());
        create_process_bus();
        m_processBus->set_id(get_id());

        TTrack::set_state(node);

        m_processBus->set_name(m_name);

        return 1;
}

void TBusTrack::create_process_bus()
{
        // avoid creating the bus twice!
        if (m_processBus) {
                return;
        }
        m_type = TTrack::TRACKTYPE::BUS;
        TAudioBusConfiguration busConfig;
        busConfig.name = m_name;
        busConfig.channelcount = m_channelCount;
        busConfig.type = "output";
        busConfig.isInternalBus = true;
        busConfig.id = get_id();
        m_processBus = new AudioBus(m_session->audio_device(), busConfig);
}

void TBusTrack::set_name( const QString & name )
{
        m_processBus->set_name(name);
        TTrack::set_name(name);
}

int TBusTrack::process(TProcessContext &context) T_REALTIME_SAFE
{
    // Real-time Accumulation Protection: If the bus is muted or resting at absolute silence,
    // we MUST evacuate the streaming buffers to prevent scaling explosions from incoming track sends.
    if (m_isMuted || (TFloatCompare::equals_0(get_gain()))) {
        if (m_processBus) {
            // Instantly wipe the shared summing bus clean for this hardware block duration
            m_processBus->silence_buffers();
        }
        return 0; // Terminate early safely; buffer is clear and zeroed
    }

    nframes_t nframes = context.get_nframes_to_process();
    const TTimeRef startLocation = context.get_start_location();
    const TTimeRef endLocation = context.get_end_location();


    process_pre_sends(nframes);

    m_pluginChain->process_pre_fader(m_processBus, nframes);

    float panFactor;

    if ( (m_processBus->get_channel_count() >= 1) && (m_pan > 0) )  {
        panFactor = 1 - m_pan;
        m_processBus->get_buffer(0).apply_gain_to_buffer(nframes, panFactor);
    }

    if ( (m_processBus->get_channel_count() >= 2) && (m_pan < 0) )  {
        panFactor = 1 + m_pan;
        m_processBus->get_buffer(1).apply_gain_to_buffer(nframes, panFactor);
    }

    m_fader->process_gain(m_processBus, startLocation, endLocation, nframes, m_processBus->get_channel_count());

    m_pluginChain->process_post_fader(m_processBus, nframes);

    m_processBus->process_monitoring(m_vumonitors);

    process_post_sends(nframes);

    m_processBus->silence_buffers();

    return 1;
}
