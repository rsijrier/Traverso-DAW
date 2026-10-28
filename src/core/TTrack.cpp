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

#include "TTrack.h"
#include "TRtTransaction.h"

#include "AudioBus.h"
#include "TAudioDevice.h"
#include "TAddRemoveCommand.h"
#include "AudioChannel.h"
#include "TAudioPluginChain.h"
#include "TSheet.h"
#include "TProjectManager.h"
#include "TProject.h"
#include "TAudioBusConfiguration.h"
#include "TVUMonitor.h"
#include "Utils.h"
#include "TBusTrack.h"
#include "TSend.h"

#include "Debugger.h"

TTrack::TTrack(TSession* session)
    : TAudioProcessingNode(session)
    , m_rtInputBus([this]() { return std::make_shared<AudioBus*>(m_inputBus); }, std::make_shared<AudioBus*>(nullptr))
{
    m_sortIndex = -1;
    m_isSolo = m_mutedBySolo = m_isMuted = false;
    m_showTrackVolumeAutomation = false;
    m_preSendOn = false;
    m_channelCount = 2;

    for (int i=0; i<2; ++i) {
        m_vumonitors.append(new TVUMonitor());
    }

    TProject* project = pm().get_project();
    if (project) {
        connect(this, &TTrack::routingConfigurationChanged, project, &TProject::track_property_changed);
    }
}

TTrack::~TTrack()
{
    // FIXME, we delete ourselves, but audiodevice could still be
    // monitoring our monitors!!!!
    for (int i=0; i<2; ++i) {
        delete m_vumonitors.at(i);
    }
}


void TTrack::get_state(QDomDocument& doc, QDomElement& node, bool istemplate)
{
    if (! istemplate ) {
        node.setAttribute("id", get_id());
    } else {
        node.setAttribute("id", TraversoDAW::Utils::create_id());
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

    for (TSend* send : m_postSends.gui_items()) {
        sendsNode.appendChild(send->get_state(node.toDocument()));
    }
    for (TSend* send : m_preSends.gui_items()) {
        sendsNode.appendChild(send->get_state(node.toDocument()));
    }

    node.appendChild(sendsNode);
}


int TTrack::set_state( const QDomNode & node )
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
        // A transaction opened while another one is open joins it: it hands its
        // changes to the outermost one and publishes nothing itself. When a
        // project loads, the outermost one is the transaction TProjectManager
        // opens around the load, so the sends reach the audio thread with the
        // whole project in one event. Otherwise this one publishes them.
        TRtTransaction transaction;

        QDomNode sendNode = sendsNode.firstChild();
        while (!sendNode.isNull()) {
            TSend* send = new TSend(this);
            if (send->set_state(sendNode) < 0) {
                // This send could not set it's state...
                TLOG_W("track", "a send could not restore its state, moving on");
                delete send;
            } else {
                if (send->get_type() == TSend::POSTSEND) {
                    private_add_post_send(send, transaction);
                }
                if (send->get_type() == TSend::PRESEND) {
                    private_add_pre_send(send, transaction);
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
    if (! m_session->is_project_session()) {
        TProject* project = pm().get_project();
        if (m_session && project) {
            qint64 sendBusId;
            if (m_name == "Sheet Master") {
                TLOG_D("track", "sheet master gets the project bus id");
                sendBusId = project->get_master_out_bus_track()->get_id();
            } else {
                sendBusId = m_session->get_master_out_bus_track()->get_id();
            }

            // Make sure we output always to the most logical bus.
            // This might conflict with what the user asked for but then
            // Use MUTE instead :)
            if (m_postSends.gui_items().isEmpty()) {
                add_post_send(sendBusId);
            }
        }
    }

    return 1;
}


TCommand* TTrack::solo(  )
{
    TSheet* sheet = qobject_cast<TSheet*>(m_session);

    // Not all Tracks have a sheet (e.g. Project Master)
    if (!sheet) {
        return nullptr;
    }

    sheet->solo_track(this);
    return nullptr;
}

TCommand* TTrack::toggle_presend()
{
    m_preSendOn = !m_preSendOn;

    emit preSendChanged(m_preSendOn);

    return  nullptr;
}

void TTrack::toggle_show_gain_automation_curve()
{
    m_showTrackVolumeAutomation = !m_showTrackVolumeAutomation;
    emit automationVisibilityChanged();
}

bool TTrack::is_solo()
{
    return m_isSolo;
}

bool TTrack::is_muted_by_solo()
{
    return m_mutedBySolo;
}


void TTrack::set_muted_by_solo(bool muted)
{
    PENTER;
    m_mutedBySolo = muted;
    emit audibleStateChanged();
}

void TTrack::set_solo(bool solo)
{
    m_isSolo = solo;
    if (solo)
        m_mutedBySolo = false;
    emit soloChanged(m_isSolo);
    emit audibleStateChanged();
}

void TTrack::set_sort_index( int index )
{
    m_sortIndex = index;
}

void TTrack::set_name( const QString & name )
{
    TAudioProcessingNode::set_name(name);

    // 'broadcast' our name change
    if (pm().get_project()) {
        pm().get_project()->track_property_changed();
    }
}


int TTrack::get_sort_index( ) const
{
    return m_sortIndex;
}

void TTrack::add_input_bus(AudioBus *bus)
{
    TRtTransaction transaction;
    set_input_bus(bus, transaction);
}

// The audio thread sees the new bus when the transaction is applied, at the
// start of a process cycle; the GUI sees it at once
void TTrack::set_input_bus(AudioBus* bus, TRtTransaction& transaction)
{
    m_inputBus = bus;
    m_rtInputBus.mark_changed(transaction);
    emit routingConfigurationChanged();
}


void TTrack::add_input_bus(qint64 busId)
{
    TProject* project = pm().get_project();
    AudioBus* bus = project->get_audio_bus(busId);
    add_input_bus(bus);
}

void TTrack::add_post_send(qint64 busId)
{
    for (TSend* send : m_postSends.gui_items()) {

        if (send->get_bus_id() == busId) {
            TLOG_W("track", "track {} already has bus {} as post send", m_name, busId);
            return;
        }
    }

    TProject* project = pm().get_project();
    AudioBus* bus = project->get_audio_bus(busId);

    if (!bus) {
        TLOG_W("track", "bus {} not found in the project", busId);
        return;
    }

    add_post_send(bus);
}

void TTrack::add_post_send(AudioBus *bus)
{
    TSend* postSend = new TSend(this, bus);
    postSend->set_type(TSend::POSTSEND);

    {
        TRtTransaction transaction;
        private_add_post_send(postSend, transaction);
    }

    emit routingConfigurationChanged();
}


void TTrack::add_pre_send(qint64 busId)
{
    for (TSend* send : m_preSends.gui_items()) {
        if (send->get_bus_id() == busId) {
            TLOG_W("track", "track {} already has bus {} as pre send", m_name, busId);
            return;
        }
    }

    TProject* project = pm().get_project();
    AudioBus* bus = project->get_audio_bus(busId);

    if (!bus) {
        TLOG_W("track", "bus {} not found in the project", busId);
        return;
    }

    TSend* preSend = new TSend(this, bus);
    preSend->set_type(TSend::PRESEND);

    {
        TRtTransaction transaction;
        private_add_pre_send(preSend, transaction);
    }

    emit routingConfigurationChanged();
}

void TTrack::remove_post_sends(QList<qint64> sendIds)
{
    QList<TSend*> sendsToBeRemoved;

    for(qint64 id : sendIds)
    {
        for (TSend* send : m_postSends.gui_items()) {
            if (send->get_id() == id) {
                sendsToBeRemoved.append(send);
            }
        }
    }

    {
        TRtTransaction transaction;
        for(TSend* send : sendsToBeRemoved) {
            private_remove_post_send(send, transaction);
        }
    }

    emit routingConfigurationChanged();
}

void TTrack::remove_post_send(TSend *send)
{
    {
        TRtTransaction transaction;
        private_remove_post_send(send, transaction);
    }

    emit routingConfigurationChanged();
}

void TTrack::remove_all_post_sends()
{
    // A copy: removing changes the list
    const QList<TSend*> sends = m_postSends.gui_items();

    {
        TRtTransaction transaction;
        for (TSend* send : sends) {
            private_remove_post_send(send, transaction);
        }
    }

    emit routingConfigurationChanged();
}

void TTrack::remove_pre_sends(QList<qint64> sendIds)
{
    QList<TSend*> sendsToBeRemoved;

    for(qint64 id : sendIds) {
        for (TSend* send : m_preSends.gui_items()) {
            if (send->get_id() == id) {
                sendsToBeRemoved.append(send);
            }
        }
    }

    {
        TRtTransaction transaction;
        for(TSend* send : sendsToBeRemoved) {
            private_remove_pre_send(send, transaction);
        }
    }

    emit routingConfigurationChanged();
}

void TTrack::private_add_post_send(TSend* postSend, TRtTransaction& transaction)
{
    m_postSends.add(postSend, transaction);
}

void TTrack::private_remove_post_send(TSend* postSend, TRtTransaction& transaction)
{
    m_postSends.remove(postSend, transaction);
}

void TTrack::private_remove_pre_send(TSend* preSend, TRtTransaction& transaction)
{
    m_preSends.remove(preSend, transaction);
}

void TTrack::private_add_pre_send(TSend* preSend, TRtTransaction& transaction)
{
    m_preSends.add(preSend, transaction);
}


void TTrack::add_input_bus(const QString &name)
{
    m_busInName = name;

    AudioBus* inBus = pm().get_project()->get_capture_bus(m_busInName);
    if (inBus) {
        add_input_bus(inBus);
    }
}

void TTrack::process_post_sends(nframes_t nframes) T_REALTIME_SAFE
{
    for (TSend* send : m_postSends.rt_items()) {
        process_send(send, nframes);
    }
}

void TTrack::process_pre_sends(nframes_t nframes) T_REALTIME_SAFE
{
    for (TSend* send : m_preSends.rt_items()) {
        process_send(send, nframes);
    }
}

void TTrack::process_send(TSend *send, nframes_t nframes)
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

            TChannelBuffer::mix_buffers_with_gain(receiver->get_buffer(), sender->get_buffer(), nframes, gainFactor);
        }

    }
}

QList<TSend* > TTrack::get_post_sends() const
{
    return m_postSends.gui_items();
}

QList<TSend* > TTrack::get_pre_sends() const
{
    return m_preSends.gui_items();
}

bool TTrack::has_post_send(qint64 busId) const
{
    for (TSend* send : m_postSends.gui_items()) {
        if (send->get_bus_id() == busId) {
            return true;
        }
    }

    return false;
}

TSend* TTrack::get_send(qint64 sendId)
{
    for (TSend* send : m_postSends.gui_items()) {
        if (send->get_id() == sendId) {
            return send;
        }
    }
    for (TSend* send : m_preSends.gui_items()) {
        if (send->get_id() == sendId) {
            return send;
        }
    }

    return 0;
}

bool TTrack::connect_to_jack(bool inports, bool outports)
{

    QString driver = m_session->audio_device().get_driver_type();
    if (driver != "Jack") {
        //                PERROR("Can't connect this Track (%s) to jack if jack isn't used as driver", QS_C(m_name));
        return false;
    }

    if (m_channelCount == 0) {
        PERROR("Channel count == 0");
        return false;
    }

    TProject* project = pm().get_project();
    AudioBus* bus = 0;

    TAudioBusConfiguration busconfig;
    busconfig.channelcount = m_channelCount;
    busconfig.name = m_name;

    TAudioChannelConfiguration channelconfig;

    if (outports) {
        for (int chan=0; chan<m_channelCount; ++chan) {
            QString portDesignation;
            if (m_channelCount == 2 && chan == 0) {
                portDesignation = "_out_1";
            } else if (m_channelCount == 2 && chan == 1) {
                portDesignation = "_out_2";
            } else {
                portDesignation = "_out";
            }

            channelconfig.name = m_name + portDesignation;
            channelconfig.name = channelconfig.name.replace(" ", "_");
            channelconfig.type = "output";
            busconfig.channelNames << channelconfig.name;
        }

        busconfig.type = "output";

        bus = project->create_software_audio_bus(busconfig);
        add_post_send(bus);
    }

    if (inports) {
        for (int chan=0; chan<m_channelCount; ++chan) {
            QString portDesignation;
            if (m_channelCount == 2 && chan == 0) {
                portDesignation = "_in_1";
            } else if (m_channelCount == 2 && chan == 1) {
                portDesignation = "_in_2";
            } else {
                portDesignation = "_in";
            }

            channelconfig.name = m_name + portDesignation;
            channelconfig.name = channelconfig.name.replace(" ", "_");
            channelconfig.type = "input";
            busconfig.channelNames << channelconfig.name;
        }

        busconfig.type = "input";

        bus = project->create_software_audio_bus(busconfig);
        add_input_bus(bus);
    }


    return true;
}

bool TTrack::disconnect_from_jack(bool inports, bool outports)
{
    TProject* project = pm().get_project();

    if (inports && m_inputBus) {
        project->remove_software_audio_bus(m_inputBus);
    }

    if (outports) {
        QList<qint64> jackSends;
        for (TSend* send : m_postSends.gui_items()) {
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

void TTrack::set_channel_count(int count)
{
    m_channelCount = count;
}
