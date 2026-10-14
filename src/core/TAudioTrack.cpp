/*
Copyright (C) 2005-2007 Remon Sijrier

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

#include "TAudioTrack.h"

#include <QDomElement>
#include <QDomNode>

#include "TSheet.h"
#include "TAudioClip.h"
#include "TAudioClipManager.h"
#include "AudioBus.h"
#include "TAudioDevice.h"
#include "TAudioPluginChain.h"
#include "TInformUser.h"
#include "TProjectManager.h"
#include "TResourcesManager.h"
#include "TLocation.h"
#include "Utils.h"
#include "Debugger.h"


TAudioTrack::TAudioTrack(TSheet* sheet, const QString& name, int height )
    : TTrack(sheet)
    , m_sheet(sheet)
{
    PENTERCONS;
    m_name = name;
    sheet->set_track_height(get_id(), height);
    m_pan = m_numtakes = 0;
    m_showClipVolumeAutomation = false;

    m_busInName = "Capture 1-2";

    init();
}

TAudioTrack::TAudioTrack( TSheet * sheet, const QDomNode node)
    : TTrack(sheet)
    , m_sheet(sheet)
{
    PENTERCONS;
    Q_UNUSED(node);
    init();
}


TAudioTrack::~TAudioTrack()
{
    PENTERDES;
}

void TAudioTrack::init()
{
    QObject::tr("TAudioTrack");

    m_type = TRACKTYPE::AUDIO;
    m_isArmed = false;
    m_processBus = m_sheet->get_render_bus();

    m_clips.set_sort_function([](const TAudioClip* left, const TAudioClip* right) {
        return left->get_location_start() < right->get_location_start();
    });
}

QDomNode TAudioTrack::get_state( QDomDocument doc, bool istemplate)
{
    QDomElement node = doc.createElement("Track");
    TTrack::get_state(doc, node, istemplate);

    node.setAttribute("numtakes", m_numtakes);
    node.setAttribute("showclipvolumeautomation", m_showClipVolumeAutomation);
    node.setAttribute("InputBus", m_busInName);


    if (! istemplate ) {
        QDomNode clips = doc.createElement("Clips");

        for(TAudioClip* clip: m_clips.items()) {
            if (clip->get_length() == qint64(0)) {
                PERROR("Clip length is 0! This shouldn't happen!!!!");
                continue;
            }

            QDomElement clipNode = doc.createElement("Clip");
            clipNode.setAttribute("id", clip->get_id() );
            clips.appendChild(clipNode);
        }

        node.appendChild(clips);
    }

    return node;
}

TTimeRef TAudioTrack::get_end_location() const
{
    TTimeRef endLocation{};
    if (!m_clips.items().isEmpty()) {
        endLocation = m_clips.items().last()->get_location_end();
    }
    return endLocation;
}


int TAudioTrack::set_state( const QDomNode & node )
{
    QDomElement e = node.toElement();

    TTrack::set_state(node);

    m_numtakes = e.attribute( "numtakes", "").toInt();
    m_showClipVolumeAutomation = e.attribute("showclipvolumeautomation", nullptr).toInt();

    QDomElement ClipsNode = node.firstChildElement("Clips");
    QList<TAudioClip*> clipsToAdd;
    if (!ClipsNode.isNull()) {
        QDomNode clipNode = ClipsNode.firstChild();
        while (!clipNode.isNull()) {
            QDomElement clipElement = clipNode.toElement();
            qint64 id = clipElement.attribute("id", "").toLongLong();

            TAudioClip* clip = resources_manager()->get_clip(id);
            if (!clip) {
                tInformUser().critical(tr("Track: AudioClip with id %1 not "
                                   "found in Resources database!").arg(id));
                break;
            }

            clip->set_sheet(m_sheet);
            clip->set_track(this);
            clip->set_state(clip->get_dom_node());
            m_sheet->get_audioclip_manager()->add_clip(clip);

            clipsToAdd.append(clip);

            clipNode = clipNode.nextSibling();
        }
    }

    // Loading a project, publish all clips of this track as one event
    TRtTransaction transaction;
    apply_clip_mutation(clipsToAdd, {});

    return 1;
}



int TAudioTrack::arm()
{
    PENTER;
    set_armed(true);
    return 1;
}


int TAudioTrack::disarm()
{
    PENTER;
    set_armed(false);
    return 1;
}

bool TAudioTrack::armed()
{
    return m_isArmed;
}

TAudioClip* TAudioTrack::init_recording()
{
    PENTER2;

    if(!m_isArmed) {
        return nullptr;
    }

    if (!m_inputBus) {
        tInformUser().critical(tr("Unable to Record to AudioTrack"));
        tInformUser().warning(tr("AudioDevice doesn't have this Capture Bus: %1 (Track %2)").
                       arg(m_busInName).arg(get_id()) );
        return nullptr;
    }

    QString name = 	m_sheet->get_name() + "-" + m_name + "-take-" + QString::number(++m_numtakes);

    TAudioClip* clip = resources_manager()->new_audio_clip(name);
    clip->set_sheet(m_sheet);
    clip->set_track(this);
    clip->move_to_location(m_sheet->get_transport_location());

    if (clip->init_recording() < 0) {
        PERROR("Could not create AudioClip to record to!");
        resources_manager()->destroy_clip(clip);
        return nullptr;
    }

    return clip;
}

void TAudioTrack::set_armed( bool armed )
{
    m_isArmed = armed;
    if (m_inputBus) {
        if (m_isArmed) {
            for (uint i=0; i<m_inputBus->get_channel_count(); i++) {
                m_inputBus->get_channel(i)->add_monitor(m_vumonitors.at(int(i)));
            }
        } else {
            for (uint i=0; i<m_inputBus->get_channel_count(); i++) {
                m_inputBus->get_channel(i)->remove_monitor(m_vumonitors.at(int(i)));
            }
        }
    }

    emit armedChanged(m_isArmed);
}

void TAudioTrack::add_input_bus(AudioBus *bus)
{
    //        if (m_inputBus/* && m_isArmed*/) {
    //                for (int i=0; i<m_inputBus->get_channel_count(); i++) {
    //                        m_inputBus->get_channel(i)->remove_monitor(m_vumonitors.at(i));
    //                }
    //        }
    ////        if (m_isArmed) {
    //                for (int i=0; i<bus->get_channel_count(); i++) {
    //                        if (bus->get_channel_count() < i) {
    //                                bus->get_channel(i)->add_monitor(m_vumonitors.at(i));
    //                        }
    //                }
    ////        }
    TTrack::add_input_bus(bus);
}

//
//  Function called in RealTime AudioThread processing path
//
int TAudioTrack::process(TProcessCallBackData &processData)
{
    int processResult = 0;

    if ( (m_isMuted || m_mutedBySolo) && ( ! m_isArmed) ) {
        return 0;
    }

    nframes_t nframes = processData.get_nframes_to_process();
    const TTimeRef startLocation = processData.get_start_location();
    const TTimeRef endLocation = processData.get_end_location();


    // Get the 'render bus' from sheet, a bit hackish solution, but
    // it avoids to have a dedicated render bus for each Track,
    // or buffers located on the heap...
    m_processBus->silence_buffers();

    int result;
    float panFactor;


    // Read in clip data into process bus.
    for(auto clip : m_clips.rt_items())
    {
        if (m_isArmed && clip->recording_state() == TAudioClip::NO_RECORDING) {
            if (m_isMuted || m_mutedBySolo) {
                continue;
            }
        }

        result = clip->process(processData);

        if (result <= 0) {
            continue;
        }

        processResult |= result;
    }

    // Then do the pre-send:
    process_pre_sends(nframes);


    // Then apply the pre fader plugins;
    m_pluginChain->process_pre_fader(m_processBus, nframes);


    // Apply PAN
    if ( (m_processBus->get_channel_count() >= 1) && (m_pan > 0) )  {
        panFactor = 1 - m_pan;
        m_processBus->get_buffer(0).apply_gain_to_buffer(nframes, panFactor);
    }

    if ( (m_processBus->get_channel_count() >= 2) && (m_pan < 0) )  {
        panFactor = 1 + m_pan;
        m_processBus->get_buffer(1).apply_gain_to_buffer(nframes, panFactor);
    }

    // Apply fader Gain/envelope
    m_fader->process_gain(m_processBus, startLocation, endLocation, nframes, m_processBus->get_channel_count());

    // Post fader plugins now
    processResult |= m_pluginChain->process_post_fader(m_processBus, nframes);

    // TODO: is there a situation where we still want to call process_post_sends
    // even if processresult == 0?
    if (processResult) {
        if (!m_isArmed) {
            m_processBus->process_monitoring(m_vumonitors);
        }

        // And finally do the post sends
        process_post_sends(nframes);
    }

    if (m_type == BOUNCE) {
        m_inputBus->process_monitoring(m_vumonitors);
        m_inputBus->silence_buffers();
    }

    return processResult;
}


TCommand* TAudioTrack::toggle_arm()
{
    if (m_isArmed) {
        disarm();
    } else {
        arm();
    }
    return nullptr;
}


bool TAudioTrack::get_export_range(TTimeRef& trackExportStartLocation, TTimeRef& trackExportEndLocation )
{
    if(m_clips.items().isEmpty()) {
        return false;
    }

    trackExportStartLocation = TTimeRef::max_length();
    trackExportEndLocation = TTimeRef();

    for(TAudioClip* clip : m_clips.items()) {
        if (! clip->is_muted() ) {
            if (clip->get_location_end() > trackExportEndLocation) {
                trackExportEndLocation = clip->get_location_end();
            }

            if (clip->get_location_start() < trackExportStartLocation) {
                trackExportStartLocation = clip->get_location_start();
            }
        }
    }

    return (trackExportStartLocation != TTimeRef::max_length() && trackExportEndLocation != TTimeRef());
}

TAudioClip* TAudioTrack::get_clip_after(const TTimeRef& pos) const
{
    for(TAudioClip* clip : m_clips.items()) {
        if (clip->get_location_start() > pos) {
            return clip;
        }
    }
    return nullptr;
}

TAudioClip *TAudioTrack::get_audio_clip_after(TAudioClip *audioClip) const
{
    auto index = m_clips.items().indexOf(audioClip);
    if (index >= 0 && (index + 1) < m_clips.items().size()) {
        return m_clips.items().at(index + 1);
    }

    return nullptr;
}

TAudioClip *TAudioTrack::get_audio_clip_before(TAudioClip *audioClip) const
{
    auto index = m_clips.items().indexOf(audioClip);
    if ((index - 1) >= 0) {
        return m_clips.items().at(index - 1);
    }

    return nullptr;
}


TAudioClip* TAudioTrack::get_clip_before(const TTimeRef& pos) const
{
    TTimeRef shortestDistance = TTimeRef::max_length();
    TAudioClip* nearest = nullptr;

    for(TAudioClip* clip : m_clips.items()) {
        if (clip->get_location_start() < pos) {
            TTimeRef diff = pos - clip->get_location_start();
            if (diff < shortestDistance) {
                shortestDistance = diff;
                nearest = clip;
            }
        }
    }

    return nearest;
}


TAudioClip *TAudioTrack::get_clip_at_location(const TTimeRef &location) const
{
    for(TAudioClip* clip : m_clips.items()) {
        if (clip->get_location_start() < location && clip->get_location_end() > location) {
            return clip;
        }
    }
    return nullptr;
}

void TAudioTrack::clip_position_changed(TAudioClip* clip)
{
    // Clips that are not (yet) on this track, e.g. while a split prepares its
    // new clips, don't affect the order of this track's clips.
    if (m_clips.items().contains(clip)) {
        m_clips.resort();
    }
}

void TAudioTrack::apply_clip_mutation(const QList<TAudioClip*>& clipsToAdd, const QList<TAudioClip*>& clipsToRemove)
{
    for (auto* clip : clipsToRemove) {
        m_clips.remove(clip);
        clip->removed_from_track();
        emit audioClipRemoved(clip);
    }

    for (auto* clip : clipsToAdd) {
        m_clips.add(clip);
        clip->set_track(this);
        emit audioClipAdded(clip);
    }
}

void TAudioTrack::toggle_show_clip_volume_automation()
{
    m_showClipVolumeAutomation = !m_showClipVolumeAutomation;
    emit automationVisibilityChanged();
}

TBounceTrack::TBounceTrack(TSheet *sheet, const QString &name, int height)
    : TAudioTrack(sheet, name, height)
{
    m_type = TTrack::TRACKTYPE::BOUNCE;
    TAudioBusConfiguration busConfig;
    busConfig.name = "Bounce Input Bus";
    busConfig.channelcount = 2;
    busConfig.type = "input";
    busConfig.isInternalBus = true;

    m_inputBus = new AudioBus(busConfig);
}

TBounceTrack::~TBounceTrack()
{
    delete m_inputBus;
}
