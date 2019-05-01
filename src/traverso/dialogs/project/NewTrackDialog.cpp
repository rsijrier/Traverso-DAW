/*
Copyright (C) 2007-2019 Remon Sijrier

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


#include "NewTrackDialog.h"
#include <QPushButton>
#include <QRadioButton>

#include "AudioDevice.h"
#include "AudioBus.h"
#include "Information.h"
#include "Project.h"
#include "ProjectManager.h"
#include "Sheet.h"
#include "TBusTrack.h"
#include "AudioTrack.h"

#include <CommandGroup.h>

NewTrackDialog::NewTrackDialog(QWidget * parent)
	: QDialog(parent)
{
    setupUi(this);

    set_project(pm().get_project());

    update_buses_comboboxes();
    monoRadioButton->setChecked(true);
    reset_information_label();
    m_timer.setSingleShot(true);
    m_completer.setFilterMode(Qt::MatchContains);
    m_completer.setCaseSensitivity(Qt::CaseInsensitive);

    connect(&pm(), SIGNAL(projectLoaded(Project*)), this, SLOT(set_project(Project*)));
    connect(closeButton, SIGNAL(clicked()), this, SLOT(close_clicked()));
    connect(addTrackBusButton, SIGNAL(clicked()), this, SLOT(create_track()));
    connect(isBusTrack, SIGNAL(toggled(bool)), this, SLOT(update_buses_comboboxes()));
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(reset_information_label()));
    connect(trackName, SIGNAL(textChanged(const QString&)), SLOT(update_completer(const QString&)));
}

void NewTrackDialog::showEvent(QShowEvent */*event*/)
{
        update_driver_info();
        if (m_project->get_current_session() == m_project) {
                isAudioTrack->hide();
                isBusTrack->setChecked(true);
        } else {
                isAudioTrack->show();
                isAudioTrack->setChecked(true);
        }
}

void NewTrackDialog::create_track()
{
	if (! m_project) {
        info().information(tr("New Track cannot be created if there is no Project loaded!!"));
		return;
	}
	
        TSession* session = m_project->get_current_session();
        if ( ! session ) {
		return ;
	}
	
        QString title = trackName->text();
	
	if (title.isEmpty()) {
		title = "Untitled";
	}
	
        QString driver = audiodevice().get_driver_type();
        Sheet* sheet = qobject_cast<Sheet*>(session);
        Track* track;

        if (isBusTrack->isChecked()) {
                track = new TBusTrack(session, title, 2);

        } else {
                track = new AudioTrack(sheet, title, AudioTrack::INITIAL_HEIGHT);
        }

        int channelCount = 1;
        if (stereoRadioButton->isChecked()) {
            channelCount = 2;
        }
        track->set_channel_count(channelCount);


        TCommand* command = session->add_track(track);
        command->setText(tr("Added %1: %2").arg(track->metaObject()->className()).arg(track->get_name()));
        TCommand::process_command(command);

        if (driver == "Jack") {
                track->connect_to_jack(true, true);
        } else {
            AudioTrack* audioTrack = qobject_cast<AudioTrack*>(track);
            TBusTrack* busTrack = qobject_cast<TBusTrack*>(track);

            QList<QListWidgetItem*> selectedItems = routingInputListWidget->selectedItems();
            foreach(QListWidgetItem* item, selectedItems) {
                // add external input (AudioTrack only)
                if (audioTrack) {
                    track->add_input_bus(item->text());
                }
                // If new track is a Bus Track, and the selected item is an AudioTrack
                // then we route AudioTrack to this Bus by adding Bus to the post send
                // of AudioTrack.
                // If AudioTrack already had any post sends then we remove it by default here
                if (busTrack) {
                    qint64 audioTrackId = item->data(Qt::UserRole).toLongLong();
                    AudioTrack* inputAudioTrack = qobject_cast<AudioTrack*>(pm().get_project()->get_track(audioTrackId));
                    if (inputAudioTrack) {
                        inputAudioTrack->remove_all_post_sends();
                        inputAudioTrack->add_post_send(busTrack->get_process_bus());
                    }
                }
            }

            // add post send to Track/Bus if any was selected
            QList<QListWidgetItem*> selectedOutputItems = routingOutputListWidget->selectedItems();
            foreach(QListWidgetItem* item, selectedOutputItems) {
                qint64 outputBusId = item->data(Qt::UserRole).toLongLong();
                track->add_post_send(outputBusId);
            }
        }

        QString styleSheet = "color: darkGreen; background: lightGreen; padding: 5px; border: solid 1px;";
        informationLabel->setStyleSheet(styleSheet);
        informationLabel->setText(tr("Created new Track '%1'' in Sheet '%2'").arg(track->get_name(), session->get_name()));

        m_timer.start(2000);

        trackName->setFocus();
        trackName->setText("");
}

void NewTrackDialog::update_completer(const QString &text)
{
    if (!isBusTrack->isChecked()) {
        return;
    }

    routingInputListWidget->setCurrentRow(-1, QItemSelectionModel::Clear);

    if (text.isEmpty()) {
        return;
    }

    m_completer.setCompletionPrefix(text);

    for (int i = 0; m_completer.setCurrentRow(i); i++) {
        for(int j = 0; j < routingInputListWidget->count(); ++j) {
            QListWidgetItem* item = routingInputListWidget->item(j);
            if (item->text() == m_completer.currentCompletion()) {
                routingInputListWidget->setCurrentRow(j, QItemSelectionModel::Select);
            }
        }
    }
}

void NewTrackDialog::set_project(Project * project)
{
	m_project = project;
}

void NewTrackDialog::update_buses_comboboxes()
{
        routingInputListWidget->clear();
        routingOutputListWidget->clear();

        TSession* session = m_project->get_current_session();

        if ( ! session ) {
                return ;
        }

        if (isBusTrack->isChecked()) {
                jackInPortsCheckBox->setChecked(false);
                routingInputListWidget->setSelectionMode(QAbstractItemView::SelectionMode::MultiSelection);
        } else {
                routingInputListWidget->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
                jackInPortsCheckBox->setChecked(true);
        }

        QListWidgetItem* item = new QListWidgetItem(routingOutputListWidget);
        item->setText(tr("Internal Buses:"));
        item->setTextColor(QColor("grey"));
        item->setFlags(Qt::NoItemFlags);

        QList<TBusTrack*> subs;
        subs.append(session->get_master_out());
        subs.append(session->get_bus_tracks());
        subs.append(m_project->get_master_out());
        foreach(TBusTrack* sg, subs) {
            QListWidgetItem* item = new QListWidgetItem(routingOutputListWidget);
            item->setText(sg->get_name());
            item->setData(Qt::UserRole, sg->get_id());
        }

        QList<AudioBus*> hardwareBuses = m_project->get_hardware_buses();

        item = new QListWidgetItem(routingOutputListWidget);
        item->setText(tr("External Outputs:"));
        item->setTextColor(QColor("grey"));
        item->setFlags(Qt::NoItemFlags);
        foreach(AudioBus* bus, hardwareBuses) {
            if (isAudioTrack->isChecked() && bus->get_type() == ChannelIsInput) {
                QListWidgetItem* item = new QListWidgetItem(routingInputListWidget);
                item->setText(bus->get_name());
                item->setData(Qt::UserRole, bus->get_id());
            }
            if (bus->get_type() == ChannelIsOutput) {
                QListWidgetItem* item = new QListWidgetItem(routingOutputListWidget);
                item->setText(bus->get_name());
                item->setData(Qt::UserRole, bus->get_id());
            }
        }

        Sheet* sheet = qobject_cast<Sheet*>(session);
        if (isBusTrack->isChecked() && sheet) {
            foreach(AudioTrack* at, sheet->get_audio_tracks()) {
                QListWidgetItem* item = new QListWidgetItem(routingInputListWidget);
                item->setText(at->get_name());
                item->setData(Qt::UserRole, at->get_id());
            }
        }


        if (isAudioTrack->isChecked()) {
            routingInputListWidget->setCurrentRow(0);
        }
        routingOutputListWidget->setCurrentRow(1);
        m_completer.setModel(routingInputListWidget->model());

        update_completer(trackName->text());
}

void NewTrackDialog::update_driver_info()
{
        QString driver = audiodevice().get_driver_type();
        if (driver == "Jack") {
                jackFrame->show();
        } else {
                jackFrame->hide();
        }
}

void NewTrackDialog::close_clicked()
{
    hide();
}

void NewTrackDialog::reset_information_label()
{
        QString styleSheet = "color: black;";
        informationLabel->setStyleSheet(styleSheet);
        informationLabel->setText(tr("Fill in Track name, and hit enter to add new Track"));
}
