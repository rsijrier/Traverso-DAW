/*
Copyright (C) 2010-2024 Remon Sijrier

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


#include "TTrackManagerDialog.h"

#include "AudioBus.h"
#include "TAudioTrack.h"
#include "TTrack.h"
#include "TProjectManager.h"
#include "TProject.h"
#include "TAudioPlugin.h"
#include "TAudioPluginChain.h"
#include "TSheet.h"
#include "TBusTrack.h"
#include "TSend.h"
#include "TThemer.h"
#include "TMainWindow.h"

#include "TGain.h"

#include <QMenu>

#include "Debugger.h"

TTrackManagerDialog::TTrackManagerDialog(TTrack *track, QWidget *parent)
    : QDialog(parent)
    , m_track(track)
{
    setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    preSendsGainPanGroupBox->setEnabled(false);
    postSendsGainPanGroupBox->setEnabled(false);

    m_routingInputMenu = m_routingOutputMenu = m_preSendsMenu = nullptr;
    m_selectedPostSend = m_selectedPreSend = nullptr;
    create_routing_input_menu();
    create_routing_output_menu();
    create_pre_sends_menu();

    update_routing_input_output_widget_view();
    update_pre_post_fader_plugins_widget_view();

    trackPanSlider->setValue(int(m_track->audio()->get_pan() * 64));
    trackGainSlider->setValue(int(TGain::to_dB(m_track->audio()->get_gain()) * 10.f));

    update_gain_indicator();
    update_pan_indicator();
    update_track_status_buttons(true);

    nameLineEdit->setText(m_track->get_name());

    if (m_track->get_type() == TTrack::AUDIO) {
        trackLabel->setText(tr("Audio Track:"));
        routingInputButton->setText("Set Input");
    }
    if (m_track->get_type() == TTrack::BUS) {
        trackLabel->setText(tr("Bus Track:"));
        routingInputButton->setText("Add Input");
    }

    prePluginsUpButton->setIcon(QIcon(":/up"));
    prePluginsDownButton->setIcon(QIcon(":/down"));
    postPluginsUpButton->setIcon(QIcon(":/up"));
    postPluginsDownButton->setIcon(QIcon(":/down"));

    MasterOutSubGroup* master = qobject_cast<MasterOutSubGroup*>(m_track);
    if (master) {
        // Master Buses are not allowed to be renamed to avoid confusion
        nameLineEdit->setEnabled(false);
        trackLabel->setText(tr("Master Bus:"));
    }
    if (m_track->get_type() == TTrack::BOUNCE) {
        setEnabled(false);
    }

    connect(m_track->audio(), &TAudioProcessingNode::panChanged, this, &TTrackManagerDialog::update_pan_indicator);
    connect(m_track, &TTrack::stateChanged, this, &TTrackManagerDialog::update_gain_indicator);
    connect(m_track, &TTrack::soloChanged, this, &TTrackManagerDialog::update_track_status_buttons);
    connect(m_track->audio(), &TAudioProcessingNode::muteChanged, this, &TTrackManagerDialog::update_track_status_buttons);
    TAudioTrack* audiotrack = qobject_cast<TAudioTrack*>(m_track);
    if (audiotrack) {
        connect(audiotrack, &TAudioTrack::armedChanged, this, &TTrackManagerDialog::update_track_status_buttons);
    } else {
        recordButton->hide();
    }
    connect(pm().get_project(), &TProject::trackPropertyChanged, this, &TTrackManagerDialog::update_routing_input_output_widget_view);

    connect(preSendsListWidget, &QListWidget::itemSelectionChanged, this, &TTrackManagerDialog::pre_sends_selection_changed);
    connect(routingOutputListWidget, &QListWidget::itemSelectionChanged, this, &TTrackManagerDialog::post_sends_selection_changed);
    connect(trackGainSlider, &QSlider::valueChanged, this, &TTrackManagerDialog::track_gain_value_changed);
    connect(trackPanSlider, &QSlider::valueChanged, this, &TTrackManagerDialog::track_pan_value_changed);
    connect(postSendsGainSlider, &QSlider::valueChanged, this, &TTrackManagerDialog::post_sends_gain_value_changed);
    connect(postSendsPanSlider, &QSlider::valueChanged, this, &TTrackManagerDialog::post_sends_pan_value_changed);
    connect(preSendsGainSlider, &QSlider::valueChanged, this, &TTrackManagerDialog::pre_sends_gain_value_changed);
    connect(preSendsPanSlider, &QSlider::valueChanged, this, &TTrackManagerDialog::pre_sends_pan_value_changed);
}

TTrackManagerDialog::~TTrackManagerDialog()
{
    PENTERDES;

    delete m_routingInputMenu;
    delete m_routingOutputMenu;
    delete m_preSendsMenu;

}

void TTrackManagerDialog::create_routing_input_menu()
{
    if (m_routingInputMenu) {
        delete m_routingInputMenu;
    }

    m_routingInputMenu = new QMenu;

    if (m_track->get_type() == TTrack::AUDIO) {
        foreach(AudioBus* bus, pm().get_project()->get_hardware_buses()) {
            if (bus->is_input() && bus->is_valid()) {
                QAction* action = m_routingInputMenu->addAction(bus->get_name());
                action->setData(bus->get_id());
            }
        }
    }

    TProject* project = pm().get_project();
    TSheet* sheet = qobject_cast<TSheet*>(m_track->get_session());
    bool isProjectBus = false;

    for(TBusTrack* track : project->get_bus_tracks()) {
        if (track == m_track) {
            isProjectBus = true;
            break;
        }
    }

    if (m_track->get_type() == TTrack::BUS) {
        QList<TTrack*> tracks;
        if (m_track == project->get_master_out_bus_track()) {
            tracks = project->get_tracks();
            tracks.append(project->get_sheet_tracks());
        } else if (isProjectBus) {
            for(TSheet* sheet : project->get_sheets()) {
                for(TAudioTrack* track : sheet->get_audio_tracks()) {
                    tracks.append(track);
                }
                for(TBusTrack* track : sheet->get_bus_tracks()) {
                    tracks.append(track);
                }
                tracks.append(sheet->get_master_out_bus_track());

            }
        } else if (sheet){
            for(TAudioTrack* at : sheet->get_audio_tracks()) {
                tracks.append(at);
            }
            if (m_track == sheet->get_master_out_bus_track()) {
                for(TBusTrack* sg : sheet->get_bus_tracks()) {
                    tracks.append(sg);
                }
            }
        }
        for(TTrack* track : tracks) {
            // Input from a track is that track's send to this one: leave out
            // the tracks for which it would close a feedback loop (T64)
            if (track->send_closes_loop(m_track->get_id())) {
                continue;
            }
            QAction* action = m_routingInputMenu->addAction(track->get_name());
            action->setData(track->get_id());
        }
    }

    routingInputButton->setMenu(m_routingInputMenu);

    connect(m_routingInputMenu, &QMenu::triggered, this, &TTrackManagerDialog::routingInputMenuActionTriggered);

}

void TTrackManagerDialog::create_routing_output_menu()
{
    if (m_routingOutputMenu) {
        delete m_routingOutputMenu;
    }

    m_routingOutputMenu = create_sends_menu();

    routingOutputButton->setMenu(m_routingOutputMenu);

    connect(m_routingOutputMenu, &QMenu::triggered, this, &TTrackManagerDialog::routingOutputMenuActionTriggered);
}

void TTrackManagerDialog::create_pre_sends_menu()
{
    if (m_preSendsMenu) {
        delete m_preSendsMenu;
    }

    m_preSendsMenu = create_sends_menu();

    preSendsButton->setMenu(m_preSendsMenu);

    connect(m_preSendsMenu, &QMenu::triggered, this, &TTrackManagerDialog::preSendsMenuActionTriggered);
}

QMenu* TTrackManagerDialog::create_sends_menu()
{
    QMenu* menu = new QMenu;
    QAction* action;

    TSheet* sheet = qobject_cast<TSheet*>(m_track->get_session());
    TProject* project = pm().get_project();

    TBusTrack* sheetMaster = nullptr;
    TBusTrack* projectMaster = project->get_master_out_bus_track();

    if (sheet) {
        sheetMaster = sheet->get_master_out_bus_track();
    }

    if (!(m_track == projectMaster)) {
        action = menu->addAction(projectMaster->get_name());
        action->setData(projectMaster->get_id());
    }

    if (sheetMaster && !(m_track == sheetMaster)) {
        action = menu->addAction(sheetMaster->get_name());
        action->setData(sheetMaster->get_id());
    }

    if (sheet) {
        QList<TBusTrack*> busTracks = sheet->get_bus_tracks();
        // FIXME: this doesn't make sense at all
        if (!(m_track->get_type() == TTrack::BUS) && busTracks.size()) {
            for(TBusTrack* busTrack : busTracks) {
                action = menu->addAction(busTrack->get_name());
                action->setData(busTrack->get_id());
            }
        }
        menu->addSeparator();
    }

    if (project) {
        QList<TBusTrack*> busTracks = project->get_bus_tracks();
        for(TBusTrack* busTrack : busTracks) {
            action = menu->addAction(busTrack->get_name());
            action->setData(busTrack->get_id());
        }
        menu->addSeparator();
    }

    // Leave out the targets a send to which would close a feedback loop
    // (T64), the track itself included
    for (QAction* target : menu->actions()) {
        if (!target->isSeparator() && m_track->send_closes_loop(target->data().toLongLong())) {
            menu->removeAction(target);
            delete target;
        }
    }

    for(AudioBus* bus : pm().get_project()->get_hardware_buses()) {
        if (bus->is_output() && bus->is_valid()) {
            action = menu->addAction(bus->get_name());
            action->setData(bus->get_id());
        }
    }

    return menu;
}

void TTrackManagerDialog::accept()
{
    QDialog::accept();

    m_track->set_name(nameLineEdit->text());
}

void TTrackManagerDialog::reject()
{
    QDialog::reject();
}

void TTrackManagerDialog::routingInputMenuActionTriggered(QAction *action)
{
    PENTER2;

    TProject* project = pm().get_project();
    if (!project) {
        return;
    }

    if (!action) {
        return;
    }

    if (m_track->get_type() == TTrack::AUDIO) {
        m_track->add_input_bus(action->text());
    }

    if (m_track->get_type() == TTrack::BUS) {
        qint64 senderId = action->data().toLongLong();
        TTrack* sender = project->get_track(senderId);
        if (sender) {
            sender->add_post_send(m_track->get_id());
        }
    }
}

void TTrackManagerDialog::routingOutputMenuActionTriggered(QAction *action)
{
    TProject* project = pm().get_project();
    if (action && project) {
        m_track->add_post_send(action->data().toLongLong());
    }
}

void TTrackManagerDialog::preSendsMenuActionTriggered(QAction *action)
{
    TProject* project = pm().get_project();
    if (action && project) {
        m_track->add_pre_send(action->data().toLongLong());
    }
}


void TTrackManagerDialog::update_routing_input_output_widget_view()
{
    routingInputListWidget->clear();

    if (m_track->get_type() == TTrack::BUS) {
        QList<TSend*> inputs = pm().get_project()->get_inputs_for_bus_track(qobject_cast<TBusTrack*>(m_track));
        foreach(TSend* send, inputs) {
            QListWidgetItem* item = new QListWidgetItem(routingInputListWidget);
            item->setText(send->get_from_name());
            item->setData(Qt::UserRole, send->get_id());
        }
    }

    //FIXME
    // What does this code actually do?
    // clang says, item is a potentential memory leak
    if (m_track->get_type() == TTrack::AUDIO) {
        QListWidgetItem* item = new QListWidgetItem(routingInputListWidget);
        AudioBus* bus = m_track->get_input_bus();
        if (bus) {
            item->setText(bus->get_name());
            if (!bus->is_valid()) {
                item->setForeground(QColor(Qt::lightGray));
            }
        }
    }


    routingOutputListWidget->clear();

    QList<TSend*> postSends = m_track->get_post_sends();
    foreach(TSend* send, postSends) {
        QListWidgetItem* item = new QListWidgetItem(routingOutputListWidget);
        AudioBus* bus = send->get_bus();
        if (bus && !bus->is_valid()) {
            item->setForeground(QColor(Qt::lightGray));
        } else {
            item->setText(bus->get_name());
        }
        item->setData(Qt::UserRole, send->get_id());
    }

    preSendsListWidget->clear();
    QList<TSend*> preSends = m_track->get_pre_sends();
    foreach(TSend* send, preSends) {
        QListWidgetItem* item = new QListWidgetItem(preSendsListWidget);
        AudioBus* bus = send->get_bus();
        if (bus && !bus->is_valid()) {
            item->setForeground(QColor(Qt::lightGray));
        } else {
            item->setText(bus->get_name());
        }
        item->setData(Qt::UserRole, send->get_id());
    }
}

void TTrackManagerDialog::update_pre_post_fader_plugins_widget_view()
{
    postFaderPluginsListWidget->clear();
    QList<TAudioPlugin*> postFaderPlugins = m_track->audio()->get_plugin_chain()->get_post_fader_plugins();
    for(TAudioPlugin* plugin : postFaderPlugins) {
        QListWidgetItem* item = new QListWidgetItem(postFaderPluginsListWidget);
        item->setText(plugin->get_name());
        item->setData(Qt::UserRole, plugin->get_id());
    }


    preFaderPluginsListWidget->clear();
    QList<TAudioPlugin*> preFaderPlugins = m_track->audio()->get_plugin_chain()->get_pre_fader_plugins();
    for(TAudioPlugin* plugin : preFaderPlugins) {
        QListWidgetItem* item = new QListWidgetItem(preFaderPluginsListWidget);
        item->setText(plugin->get_name());
        item->setData(Qt::UserRole, plugin->get_id());
    }
}

void TTrackManagerDialog::on_routingInputRemoveButton_clicked()
{
    QList<QListWidgetItem*> selectedItems = routingInputListWidget->selectedItems();
    QList<qint64> toBeRemoved;

    for(QListWidgetItem* item : selectedItems) {
        qint64 id = item->data(Qt::UserRole).toLongLong();
        toBeRemoved.append(id);
    }

    QList<TTrack*> tracks = pm().get_project()->get_sheet_tracks();
    for(TTrack* track : tracks) {
        track->remove_pre_sends(toBeRemoved);
        track->remove_post_sends(toBeRemoved);
    }

}

void TTrackManagerDialog::on_preSendsRemoveButton_clicked()
{
    QList<QListWidgetItem*> selectedItems = preSendsListWidget->selectedItems();
    QList<qint64> toBeRemoved;

    for(QListWidgetItem* item : selectedItems) {
        qint64 id = item->data(Qt::UserRole).toLongLong();
        toBeRemoved.append(id);
    }

    m_track->remove_pre_sends(toBeRemoved);
}

void TTrackManagerDialog::on_routingOutputRemoveButton_clicked()
{
    QList<QListWidgetItem*> selectedItems = routingOutputListWidget->selectedItems();
    QList<qint64> toBeRemoved;

    for(QListWidgetItem* item : selectedItems) {
        qint64 id = item->data(Qt::UserRole).toLongLong();
        toBeRemoved.append(id);
    }

    m_track->remove_post_sends(toBeRemoved);
}


void TTrackManagerDialog::update_gain_indicator()
{
    gainLabel->setText(m_track->audio()->get_gain_db_string());
}

void TTrackManagerDialog::update_pan_indicator()
{
    panLabel->setText(QByteArray::number(double(m_track->audio()->get_pan()), 'f', 2));
}

void TTrackManagerDialog::pre_sends_selection_changed()
{
    QList<QListWidgetItem*> selectedItems = preSendsListWidget->selectedItems();
    if (selectedItems.size()) {
        preSendsGainPanGroupBox->setEnabled(true);
        qint64 sendId = selectedItems.first()->data(Qt::UserRole).toLongLong();
        m_selectedPreSend = m_track->get_send(sendId);
        if (m_selectedPreSend) {
            preSendsGainSlider->setValue(int(TGain::to_dB(m_selectedPreSend->get_gain()) * 10.f));
            postSendsPanSlider->setValue(int(m_selectedPreSend->get_pan() * 64));
        }
    } else {
        preSendsGainPanGroupBox->setEnabled(false);
        m_selectedPreSend = nullptr;
    }
}

void TTrackManagerDialog::post_sends_selection_changed()
{
    QList<QListWidgetItem*> selectedItems = routingOutputListWidget->selectedItems();
    if (selectedItems.size()) {
        postSendsGainPanGroupBox->setEnabled(true);
        qint64 sendId = selectedItems.first()->data(Qt::UserRole).toLongLong();
        m_selectedPostSend = m_track->get_send(sendId);
        if (m_selectedPostSend) {
            postSendsGainSlider->setValue(int(TGain::to_dB(m_selectedPostSend->get_gain()) * 10));
            postSendsPanSlider->setValue(int(m_selectedPostSend->get_pan() * 64));
        }
    } else {
        postSendsGainPanGroupBox->setEnabled(false);
        m_selectedPreSend = nullptr;
    }
}

void TTrackManagerDialog::track_gain_value_changed(int value)
{
    float v = float(value) / 10;
    float gain = TGain::from_dB(v);
    m_track->audio()->set_gain(gain);
}

void TTrackManagerDialog::track_pan_value_changed(int value)
{
    float pan = float(value) / 64;
    m_track->audio()->set_pan(pan);
}

void TTrackManagerDialog::pre_sends_gain_value_changed(int value)
{
    if (!m_selectedPreSend) {
        return;
    }

    qreal v = value / 10;
    float gain = TGain::from_dB(float(v));
    QByteArray gainString = QByteArray::number(v, 'f', 1) + " dB";
    preSendGainLabel->setText(gainString);
    m_selectedPreSend->set_gain(gain);
}

void TTrackManagerDialog::pre_sends_pan_value_changed(int value)
{
    if (!m_selectedPreSend) {
        return;
    }

    float pan = float(value) / 64;
    QByteArray panString = QByteArray::number(double(pan), 'f', 2);
    preSendPanLabel->setText(panString);
    m_selectedPreSend->set_pan(pan);
}

void TTrackManagerDialog::post_sends_gain_value_changed(int value)
{
    if (!m_selectedPostSend) {
        return;
    }
    float v = float(value) / 10;
    float gain = TGain::from_dB(v);
    QByteArray gainString = QByteArray::number(double(v), 'f', 1) + " dB";
    postSendGainLabel->setText(gainString);
    m_selectedPostSend->set_gain(gain);
}

void TTrackManagerDialog::post_sends_pan_value_changed(int value)
{
    if (!m_selectedPostSend) {
        return;
    }

    float pan = float(value) / 64;
    QByteArray panString = QByteArray::number(double(pan), 'f', 2);
    postSendPanLabel->setText(panString);
    m_selectedPostSend->set_pan(pan);
}

void TTrackManagerDialog::on_muteButton_clicked()
{
    m_track->audio()->mute();
}

void TTrackManagerDialog::on_soloButton_clicked()
{
    m_track->solo();
}

void TTrackManagerDialog::on_recordButton_clicked()
{
    TAudioTrack* audiotrack = qobject_cast<TAudioTrack*>(m_track);
    if (audiotrack) {
        audiotrack->toggle_arm();
    }
}

void TTrackManagerDialog::on_monitorButton_clicked()
{
    //        m_track->monitor();
}

void TTrackManagerDialog::update_track_status_buttons(bool)
{
    QPalette defaultPalette = QApplication::style()->standardPalette();
    QPalette highlightedPalette = QApplication::style()->standardPalette();

    if (m_track->audio()->is_muted()) {
        highlightedPalette.setColor(QPalette::Button, themer()->get_color("TrackPanel:muteled"));
        muteButton->setPalette(highlightedPalette);
    } else {
        muteButton->setPalette(defaultPalette);
    }

    if (m_track->is_solo()) {
        highlightedPalette.setColor(QPalette::Button, themer()->get_color("TrackPanel:sololed"));
        soloButton->setPalette(highlightedPalette);
    } else {
        soloButton->setPalette(defaultPalette);
    }

    TAudioTrack* audiotrack = qobject_cast<TAudioTrack*>(m_track);
    if (audiotrack) {
        if (audiotrack->armed()) {
            highlightedPalette.setColor(QPalette::Button, themer()->get_color("TrackPanel:recled"));
            recordButton->setPalette(highlightedPalette);
        } else {
            recordButton->setPalette(defaultPalette);
        }
    }
}
