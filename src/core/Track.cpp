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

#include "AudioBus.h"
#include "AudioDevice.h"
#include "AddRemove.h"
#include "AudioChannel.h"
#include "Mixer.h"
#include "PluginChain.h"
#include "Sheet.h"
#include "ProjectManager.h"
#include "Project.h"
#include "TAudioBusConfiguration.h"
#include "Utils.h"
#include "TBusTrack.h"
#include "TSend.h"

#include "Debugger.h"

Track::Track(TSession* session)
        : TAudioProcessingNode(session)
{
        m_sortIndex = -1;
	m_isSolo = m_mutedBySolo = m_isMuted = false;
	m_showTrackVolumeAutomation = false;
	m_preSendOn = false;
        m_inputBus = nullptr;
        m_channelCount = 2;

        for (int i=0; i<2; ++i) {
                m_vumonitors.append(new TVUMonitor());
        }

        Project* project = pm().get_project();
        if (project) {
                connect(this, SIGNAL(routingConfigurationChanged()), project, SLOT(track_property_changed()));
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


void Track::get_state(QDomDocument& doc, QDomElement& node, bool istemplate)
{
        if (! istemplate ) {
        node.setAttribute("id", get_id());
        } else {
                node.setAttribute("id", create_id());
        }
        node.setAttribute("name", m_name);
        node.setAttribute("pan", m_pan);
        node.setAttribute("mute", m_isMuted);
        node.setAttribute("solo", m_isSolo);
        node.setAttribute("mutedbysolo", m_mutedBySolo);
	node.setAttribute("showtrackvolumeautomation", m_showTrackVolumeAutomation);
	node.setAttribute("sortindex", m_sortIndex);
    node.setAttribute("height", m_session->get_track_height(get_id()));

        QDomNode pluginChainNode = doc.createElement("PluginChain");
        pluginChainNode.appendChild(m_pluginChain->get_state(node.toDocument()));
        node.appendChild(pluginChainNode);

        QDomNode sendsNode = doc.createElement("Sends");

        for(TSend* send = m_postSends.first(); send != nullptr; send = send->next) {
            sendsNode.appendChild(send->get_state(node.toDocument()));
        }
        for(TSend* send = m_preSends.first(); send != nullptr; send = send->next) {
                sendsNode.appendChild(send->get_state(node.toDocument()));
        }

        node.appendChild(sendsNode);
}


int Track::set_state( const QDomNode & node )
{
        QDomElement e = node.toElement();

        m_showTrackVolumeAutomation = e.attribute("showtrackvolumeautomation", 0).toInt();

        m_sortIndex = e.attribute( "sortindex", "-1" ).toInt();
        // Sheet/Project Master tracks can have their name set before set_state() is called
        if (m_name.isEmpty()) {
            m_name = e.attribute( "name", "" );
        }
        set_muted(e.attribute( "mute", "" ).toInt());
        set_solo(e.attribute( "solo", "" ).toInt());
        set_muted_by_solo(e.attribute( "mutedbysolo", "0").toInt());
        set_pan( e.attribute( "pan", "" ).toFloat() );
        set_id(e.attribute("id", "0").toLongLong());
        m_session->set_track_height(get_id(), e.attribute( "height", "90" ).toInt());

        QDomNode m_pluginChainNode = node.firstChildElement("PluginChain");
        if (!m_pluginChainNode.isNull()) {
                m_pluginChain->set_state(m_pluginChainNode);
        }


        add_input_bus(e.attribute( "InputBus", "Capture 1-2"));

        QDomNode sendsNode = node.firstChildElement("Sends");
        if (!sendsNode.isNull()) {
                QDomNode sendNode = sendsNode.firstChild();
                while (!sendNode.isNull()) {
                        TSend* send = new TSend(this);
                        if (send->set_state(sendNode) < 0) {
                                // This send could not set it's state...
                                printf("Track::set_state: Send could not properly restore it's state, moving on..\n");
                                delete send;
                        } else {
                                if (send->get_type() == TSend::POSTSEND) {
                                        private_add_post_send(send);
                                }
                                if (send->get_type() == TSend::PRESEND) {
                                        private_add_pre_send(send);
                                }
                        }
                        sendNode = sendNode.nextSibling();
                }
        }

        // Keep old project files up to 0.49.x working, at least, try our best...
        // TODO: remove this at some point in future where everybody uses > 0.49.x
        // NOTE: it is also called on a newly created project so at this point it does
        // add default post send to a Track. In other words, this is very much needed
        // for newly created tracks
        // What about reviewing the whole create_project() and then load_project() scheme?
        if (m_postSends.isEmpty() && ! m_session->is_project_session()) {
                Project* project = pm().get_project();
                if (m_session && project) {
                    if (m_name == "Sheet Master") {
                        add_post_send(project->get_master_out_bus_track()->get_id());
                    } else {
                        add_post_send(m_session->get_master_out_bus_track()->get_id());

                    }
                }
        }

        return 1;
}


TCommand* Track::solo(  )
{
        Sheet* sheet = qobject_cast<Sheet*>(m_session);

        // Not all Tracks have a sheet (e.g. Project Master)
        if (!sheet) {
                return nullptr;
        }

        sheet->solo_track(this);
        return nullptr;
}

TCommand* Track::toggle_presend()
{
	m_preSendOn = !m_preSendOn;

	emit preSendChanged(m_preSendOn);

    return  nullptr;
}

TCommand* Track::toggle_show_gain_automation_curve()
{
	m_showTrackVolumeAutomation = !m_showTrackVolumeAutomation;
	emit automationVisibilityChanged();

    return nullptr;
}

bool Track::is_solo()
{
        return m_isSolo;
}

bool Track::is_muted_by_solo()
{
        return m_mutedBySolo;
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

void Track::set_name( const QString & name )
{
        TAudioProcessingNode::set_name(name);

        // 'broadcast' our name change
        if (pm().get_project()) {
                pm().get_project()->track_property_changed();
        }
}


int Track::get_sort_index( ) const
{
        return m_sortIndex;
}

void Track::add_input_bus(AudioBus *bus)
{
        if (m_session && m_session->is_transport_rolling()) {
        tsar().add_gui_event(this, bus, "private_add_input_bus(AudioBus*)", "routingConfigurationChanged()");
        } else {
                private_add_input_bus(bus);
                emit routingConfigurationChanged();
        }
}

void Track::remove_input_bus(AudioBus *bus)
{
        if (m_session && m_session->is_transport_rolling()) {
        tsar().add_gui_event(this, bus, "private_remove_input_bus(AudioBus*)", "routingConfigurationChanged()");
        } else {
                private_remove_input_bus(bus);
                emit routingConfigurationChanged();
        }
}

void Track::add_input_bus(qint64 busId)
{
        Project* project = pm().get_project();
        AudioBus* bus = project->get_audio_bus(busId);
        add_input_bus(bus);
}

void Track::add_post_send(qint64 busId)
{
    for(TSend* send = m_postSends.first(); send != nullptr; send = send->next) {

        if (send->get_bus_id() == busId) {
            printf("Track %s already has this bus (bus id: %lld) as Post Send\n", m_name.toLatin1().data(), busId);
            return;
        }
    }

    Project* project = pm().get_project();
    AudioBus* bus = project->get_audio_bus(busId);

    if (!bus) {
        printf("bus with id %lld could not be found by project!\n", busId);
        return;
    }

    add_post_send(bus);
}

void Track::add_post_send(AudioBus *bus)
{
    TSend* postSend = new TSend(this, bus);
    postSend->set_type(TSend::POSTSEND);

    if (!m_session || (m_session && m_session->is_transport_rolling())) {
        tsar().add_gui_event(this, postSend, "private_add_post_send(TSend*)", "routingConfigurationChanged()");
    } else {
        private_add_post_send(postSend);
        emit routingConfigurationChanged();
    }
}


void Track::add_pre_send(qint64 busId)
{
    for(TSend* send = m_preSends.first(); send != nullptr; send = send->next) {
        if (send->get_bus_id() == busId) {
            printf("Track %s already has this bus (bus id: %lld) as Pre Send\n", m_name.toLatin1().data(), busId);
            return;
        }
    }

    Project* project = pm().get_project();
    AudioBus* bus = project->get_audio_bus(busId);

    if (!bus) {
        printf("bus with id %lld could not be found by project!\n", busId);
        return;
    }

    TSend* preSend = new TSend(this, bus);
    preSend->set_type(TSend::PRESEND);

    if (!m_session || (m_session && m_session->is_transport_rolling())) {
        tsar().add_gui_event(this, preSend, "private_add_pre_send(TSend*)", "routingConfigurationChanged()");
    } else {
        private_add_pre_send(preSend);
        emit routingConfigurationChanged();
    }
}

void Track::remove_post_sends(QList<qint64> sendIds)
{
    QList<TSend*> sendsToBeRemoved;

    for(qint64 id : sendIds)
    {
        for(TSend* send = m_postSends.first(); send != nullptr; send = send->next) {
            if (send->get_id() == id) {
                sendsToBeRemoved.append(send);
            }
        }
    }

    for(TSend* send : sendsToBeRemoved) {
        remove_post_send(send);
    }
}

void Track::remove_post_send(TSend *send)
{
    if (!m_session || (m_session && m_session->is_transport_rolling())) {
        tsar().add_gui_event(this, send, "private_remove_post_send(TSend*)", "routingConfigurationChanged()");
    } else {
        private_remove_post_send(send);
        emit routingConfigurationChanged();
    }
}

void Track::remove_all_post_sends()
{
    for(TSend* send = m_postSends.first(); send != nullptr; send = send->next) {
        remove_post_send(send);
    }
}

void Track::remove_pre_sends(QList<qint64> sendIds)
{
    QList<TSend*> sendsToBeRemoved;

    for(qint64 id : sendIds) {
        for(TSend* send = m_preSends.first(); send != nullptr; send = send->next) {
            if (send->get_id() == id) {
                sendsToBeRemoved.append(send);
            }
        }
    }

    for(TSend* send : sendsToBeRemoved) {
        if (!m_session || (m_session && m_session->is_transport_rolling())) {
            tsar().add_gui_event(this, send, "private_remove_pre_send(TSend*)", "routingConfigurationChanged()");
        } else {
            private_remove_pre_send(send);
            emit routingConfigurationChanged();
        }
    }
}

void Track::private_add_post_send(TSend* postSend)
{
        m_postSends.append(postSend);
}

void Track::private_remove_post_send(TSend* postSend)
{
        m_postSends.remove(postSend);
}

void Track::private_remove_pre_send(TSend* preSend)
{
        m_preSends.remove(preSend);
}

void Track::private_add_pre_send(TSend* preSend)
{
        m_preSends.append(preSend);
}


void Track::private_add_input_bus(AudioBus* bus)
{
        m_inputBus = bus;
}

void Track::private_remove_input_bus(AudioBus *bus)
{
        if (bus == m_inputBus) {
                m_inputBus = 0;
        }
}

void Track::add_input_bus(const QString &name)
{
        m_busInName = name;

        AudioBus* inBus = pm().get_project()->get_capture_bus(m_busInName);
        if (inBus) {
                add_input_bus(inBus);
        }
}

void Track::process_post_sends(nframes_t nframes)
{
    for(TSend* send = m_postSends.first(); send != nullptr; send = send->next) {
        process_send(send, nframes);
    }
}

void Track::process_pre_sends(nframes_t nframes)
{
    for(TSend* send = m_preSends.first(); send != nullptr; send = send->next) {
        process_send(send, nframes);
    }
}

void Track::process_send(TSend *send, nframes_t nframes)
{
    AudioChannel* sender;
    AudioChannel* receiver;
    float gainFactor;
    float panFactor;

    AudioBus* receiverBus = send->get_bus();
    for (uint i=0; i<m_processBus->get_channel_count(); i++) {
        sender = m_processBus->get_channel(i);
        receiver = receiverBus->get_channel(i);
        if (sender && receiver) {
            panFactor = 1.0f;
            // Left channel
            if (i == 0) {
                panFactor = 1 - send->get_pan();
            }
            // Right channel
            if (i == 1) {
                panFactor = 1 + send->get_pan();
            }

            gainFactor = panFactor * send->get_gain();

            if (gainFactor == 1.0f) {
                Mixer::mix_buffers_no_gain(receiver->get_buffer(nframes), sender->get_buffer(nframes), nframes);
            } else {
                Mixer::mix_buffers_with_gain(receiver->get_buffer(nframes), sender->get_buffer(nframes), nframes, gainFactor);
            }
        }

    }
}

QList<TSend* > Track::get_post_sends() const
{
    QList<TSend*> sends;

    for(TSend* send = m_postSends.first(); send != nullptr; send = send->next) {
        sends.append(send);
    }
    return sends;
}

QList<TSend* > Track::get_pre_sends() const
{
    QList<TSend*> sends;

    for(TSend* send = m_preSends.first(); send != nullptr; send = send->next) {
        sends.append(send);
    }
    return sends;
}

TSend* Track::get_send(qint64 sendId)
{
    for(TSend* send = m_postSends.first(); send != nullptr; send = send->next) {
                if (send->get_id() == sendId) {
                        return send;
                }
        }
    for(TSend* send = m_preSends.first(); send != nullptr; send = send->next) {
        if (send->get_id() == sendId) {
            return send;
        }
    }

    return 0;
}

bool Track::connect_to_jack(bool inports, bool outports)
{

        QString driver = audiodevice().get_driver_type();
        if (driver != "Jack") {
//                PERROR("Can't connect this Track (%s) to jack if jack isn't used as driver", QS_C(m_name));
                return false;
        }

        if (m_channelCount == 0) {
                PERROR("Channel count == 0");
                return false;
        }

        Project* project = pm().get_project();
        AudioBus* bus = 0;

        TAudioBusConfiguration busconfig;
        busconfig.channelcount = m_channelCount;
        busconfig.name = m_name;

        TAudioChannelConfiguration channelconfig;

        if (outports) {
                for (int chan=0; chan<m_channelCount; ++chan) {
            channelconfig.name = m_name + " : " + QString("%1 : out").arg(chan);
                        channelconfig.type = "output";
                        busconfig.channelNames << channelconfig.name;
                }

                busconfig.type = "output";

                bus = project->create_software_audio_bus(busconfig);
                add_post_send(bus);
        }

        if (inports) {
                for (int chan=0; chan<m_channelCount; ++chan) {
            channelconfig.name = m_name + " : " + QString("%1 : in").arg(chan);
                        channelconfig.type = "input";
                        busconfig.channelNames << channelconfig.name;
                }

                busconfig.type = "input";

                bus = project->create_software_audio_bus(busconfig);
                add_input_bus(bus);
        }


        return true;
}

bool Track::disconnect_from_jack(bool inports, bool outports)
{
        Project* project = pm().get_project();

        if (inports && m_inputBus) {
                project->remove_software_audio_bus(m_inputBus);
        }

        if (outports) {
                QList<qint64> jackSends;
            for(TSend* send = m_postSends.first(); send != nullptr; send = send->next) {
                    if (send->get_bus()->get_bus_type() == AudioBus::BusIsSoftware) {
                                jackSends.append(send->get_id());
                                project->remove_software_audio_bus(send->get_bus());
                        }
                }
                if (jackSends.size()) {
                        remove_post_sends(jackSends);
                }
        }

        return true;
}

void Track::set_channel_count(int count)
{
        m_channelCount = count;
}
