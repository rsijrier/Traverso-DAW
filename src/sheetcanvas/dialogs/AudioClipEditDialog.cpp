/*
Copyright (C) 2007 Remon Sijrier 

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


#include "AudioClipEditDialog.h"

#include "TAudioClip.h"
#include "TBufferedAudioStreamReader.h"
#include "TFadeCurve.h"
#include "TProjectManager.h"
#include "TResourcesManager.h"
#include "TLocation.h"
#include "Mixer.h"
#include "TCommand.h"
#include "AudioClipExternalProcessing.h"
#include "TInputEventDispatcher.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTimeEdit>

#define TIME_FORMAT "hh:mm:ss.zzz"

AudioClipEditDialog::AudioClipEditDialog(TAudioClip* clip, QWidget* parent) 
	: QDialog(parent), m_clip(clip)
{
	setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

	locked = false;
	
	// Used for cancelling the changes on Cancel button activated
	QDomDocument tempDoc;
	m_origState = clip->get_state(tempDoc);
	
	clipStartEdit->setDisplayFormat(TIME_FORMAT);
	clipLengthEdit->setDisplayFormat(TIME_FORMAT);
	fadeInEdit->setDisplayFormat(TIME_FORMAT);
	fadeOutEdit->setDisplayFormat(TIME_FORMAT);

	fadeInModeBox->insertItem(1, "Bended");
	fadeInModeBox->insertItem(2, "S-Shape");
	fadeInModeBox->insertItem(3, "Long");

	fadeOutModeBox->insertItem(1, "Bended");
	fadeOutModeBox->insertItem(2, "S-Shape");
	fadeOutModeBox->insertItem(3, "Long");

    clipGainSpinBox->setSuffix(" dB");

	// Used to set gain and name
	clip_state_changed();
	
	// used for length, track start position
    audioclip_location_changed();
	
	// detect and set fade params
	fade_curve_added();
	
	connect(clip, &TAudioClip::stateChanged, this, &AudioClipEditDialog::clip_state_changed);
    connect(clip->get_location(), &TLocation::locationChanged, this, &AudioClipEditDialog::audioclip_location_changed);
    connect(clip, &TAudioClip::fadeAdded, this, &AudioClipEditDialog::fade_curve_added);
	
	connect(clipGainSpinBox, &QDoubleSpinBox::valueChanged, this, &AudioClipEditDialog::gain_spinbox_value_changed);
	
	connect(clipStartEdit, &QTimeEdit::timeChanged, this, &AudioClipEditDialog::clip_start_edit_changed);
	connect(clipLengthEdit, &QTimeEdit::timeChanged, this, &AudioClipEditDialog::clip_length_edit_changed);
	
    connect(fadeInEdit, &QTimeEdit::timeChanged, this, &AudioClipEditDialog::fadein_edit_changed);
	connect(fadeInModeBox, &QComboBox::currentIndexChanged, this, &AudioClipEditDialog::fadein_mode_edit_changed);
	connect(fadeInBendingBox, &QDoubleSpinBox::valueChanged, this, &AudioClipEditDialog::fadein_bending_edit_changed);
	connect(fadeInStrengthBox, &QDoubleSpinBox::valueChanged, this, &AudioClipEditDialog::fadein_strength_edit_changed);
	connect(fadeInLinearButton, &QPushButton::clicked, this, &AudioClipEditDialog::fadein_linear);
	connect(fadeInDefaultButton, &QPushButton::clicked, this, &AudioClipEditDialog::fadein_default);

    connect(fadeOutEdit, &QTimeEdit::timeChanged, this, &AudioClipEditDialog::fadeout_edit_changed);
	connect(fadeOutModeBox, &QComboBox::currentIndexChanged, this, &AudioClipEditDialog::fadeout_mode_edit_changed);
	connect(fadeOutBendingBox, &QDoubleSpinBox::valueChanged, this, &AudioClipEditDialog::fadeout_bending_edit_changed);
	connect(fadeOutStrengthBox, &QDoubleSpinBox::valueChanged, this, &AudioClipEditDialog::fadeout_strength_edit_changed);
	connect(fadeOutLinearButton, &QPushButton::clicked, this, &AudioClipEditDialog::fadeout_linear);
	connect(fadeOutDefaultButton, &QPushButton::clicked, this, &AudioClipEditDialog::fadeout_default);
	
	connect(externalProcessingButton, &QPushButton::clicked, this, &AudioClipEditDialog::external_processing);
	connect(buttonBox, &QDialogButtonBox::accepted, this, &AudioClipEditDialog::save_changes);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &AudioClipEditDialog::cancel_changes);
}

AudioClipEditDialog::~AudioClipEditDialog()
{
}


void AudioClipEditDialog::external_processing()
{
	TCommand::process_command(new AudioClipExternalProcessing(m_clip));
}

void AudioClipEditDialog::clip_state_changed()
{
	if (m_clip->get_name() != clipNameLineEdit->text()) {
		setWindowTitle(m_clip->get_name());
		clipNameLineEdit->setText(m_clip->get_name());
	}
	
    clipGainSpinBox->setValue(Mixer::coefficient_to_dB(m_clip->get_gain()));
    sourceLineEdit->setText(m_clip->get_readsource()->get_filename());
    sourceLineEdit->setToolTip(m_clip->get_readsource()->get_filename());
    sampleRateLable->setText(QString::number(m_clip->get_rate() / 1000.0, 'f', 1) + " KHz");
}

void AudioClipEditDialog::save_changes()
{
	hide();
	QString name = clipNameLineEdit->text();
	if (!name.isEmpty()) {
		m_clip->set_name(name);
	} else {
		clipNameLineEdit->setText(m_clip->get_name());
	}		
}

void AudioClipEditDialog::cancel_changes()
{
	hide();
	m_clip->set_state(m_origState);
}

void AudioClipEditDialog::gain_spinbox_value_changed(double value)
{
	float gain = dB_to_scale_factor(value);
	m_clip->set_gain(gain);
}

void AudioClipEditDialog::audioclip_location_changed()
{
	if (locked) return;

    QTime clipLengthTime = TTimeRef::timeref_to_qtime(m_clip->get_length());
	clipLengthEdit->setTime(clipLengthTime);
	
    QTime clipStartTime = TTimeRef::timeref_to_qtime(m_clip->get_location_start());
	clipStartEdit->setTime(clipStartTime);

	update_clip_end();
}

void AudioClipEditDialog::fadein_length_changed()
{
	if (locked) return;
	
	TTimeRef ref(qint64(m_clip->get_fade_in()->get_range()));
    QTime fadeTime = TTimeRef::timeref_to_qtime(ref);
	fadeInEdit->setTime(fadeTime);
}

void AudioClipEditDialog::fadeout_length_changed()
{
	if (locked) return;

	TTimeRef ref(qint64(m_clip->get_fade_out()->get_range()));
    QTime fadeTime = TTimeRef::timeref_to_qtime(ref);
	fadeOutEdit->setTime(fadeTime);
}

void AudioClipEditDialog::fadein_edit_changed(const QTime& time)
{
	// Hmm, we can't distinguish between hand editing the time edit
	// or moving the clip with the mouse! In the latter case this function
	// causes trouble when moving the right edge with the mouse! 
	// This 'fixes' it .....
	if (ied().is_holding()) return;

	locked = true;
    double range = double(TTimeRef::qtime_to_timeref(time).universal_frame());
    m_clip->set_fade_in_range(range);
	locked = false;
}

void AudioClipEditDialog::fadeout_edit_changed(const QTime& time)
{
	if (ied().is_holding()) return;

	locked = true;
    double range = double(TTimeRef::qtime_to_timeref(time).universal_frame());
    m_clip->set_fade_out_range(range);
	locked = false;
}

void AudioClipEditDialog::clip_length_edit_changed(const QTime& time)
{
	if (ied().is_holding()) return;

	locked = true;
	
    TTimeRef ref = TTimeRef::qtime_to_timeref(time);

	if (ref >= m_clip->get_source_length()) {
		ref = m_clip->get_source_length();
        QTime clipLengthTime = TTimeRef::timeref_to_qtime(ref);
		clipLengthEdit->setTime(clipLengthTime);
	}

    m_clip->set_right_edge(ref + m_clip->get_location_start());
	update_clip_end();
	locked = false;
}

void AudioClipEditDialog::clip_start_edit_changed(const QTime& time)
{
	if (ied().is_holding()) return;

	locked = true;
    m_clip->move_to_location(TTimeRef::qtime_to_timeref(time));
	update_clip_end();
	locked = false;
}

void AudioClipEditDialog::fadein_mode_changed()
{
	if (locked) return;

	int m = m_clip->get_fade_in()->get_mode();
	fadeInModeBox->setCurrentIndex(m);
}

void AudioClipEditDialog::fadeout_mode_changed()
{
	if (locked) return;

	int m = m_clip->get_fade_out()->get_mode();
	fadeOutModeBox->setCurrentIndex(m);
}

void AudioClipEditDialog::fadein_bending_changed()
{
	if (locked) return;
	fadeInBendingBox->setValue(m_clip->get_fade_in()->get_bend_factor());
}

void AudioClipEditDialog::fadeout_bending_changed()
{
	if (locked) return;
	fadeOutBendingBox->setValue(m_clip->get_fade_out()->get_bend_factor());
}

void AudioClipEditDialog::fadein_strength_changed()
{
	if (locked) return;
	fadeInStrengthBox->setValue(m_clip->get_fade_in()->get_strength_factor());
}

void AudioClipEditDialog::fadeout_strength_changed()
{
	if (locked) return;
	fadeOutStrengthBox->setValue(m_clip->get_fade_out()->get_strength_factor());
}

void AudioClipEditDialog::fadein_mode_edit_changed(int index)
{
    if (!m_clip->has_fade_in()) return;
	locked = true;
	m_clip->get_fade_in()->set_mode(index);
	locked = false;
}

void AudioClipEditDialog::fadeout_mode_edit_changed(int index)
{
    if (!m_clip->has_fade_out()) return;
	locked = true;
	m_clip->get_fade_out()->set_mode(index);
	locked = false;
}

void AudioClipEditDialog::fadein_bending_edit_changed(double value)
{
    if (!m_clip->has_fade_in()) return;
	locked = true;
	m_clip->get_fade_in()->set_bend_factor(value);
	locked = false;
}

void AudioClipEditDialog::fadeout_bending_edit_changed(double value)
{
    if (!m_clip->has_fade_out()) return;
	locked = true;
	m_clip->get_fade_out()->set_bend_factor(value);
	locked = false;
}

void AudioClipEditDialog::fadein_strength_edit_changed(double value)
{
    if (!m_clip->has_fade_in()) return;
	locked = true;
	m_clip->get_fade_in()->set_strength_factor(value);
	locked = false;
}

void AudioClipEditDialog::fadeout_strength_edit_changed(double value)
{
    if (!m_clip->has_fade_out()) return;
	locked = true;
	m_clip->get_fade_out()->set_strength_factor(value);
	locked = false;
}

void AudioClipEditDialog::fadein_linear()
{
    if (!m_clip->has_fade_in()) return;
	fadeInBendingBox->setValue(0.5);
	fadeInStrengthBox->setValue(0.5);
}

void AudioClipEditDialog::fadein_default()
{
    if (!m_clip->has_fade_in()) return;
	fadeInBendingBox->setValue(0.0);
	fadeInStrengthBox->setValue(0.5);
}

void AudioClipEditDialog::fadeout_linear()
{
    if (!m_clip->has_fade_out()) return;
	fadeOutBendingBox->setValue(0.5);
	fadeOutStrengthBox->setValue(0.5);
}

void AudioClipEditDialog::fadeout_default()
{
    if (!m_clip->has_fade_out()) return;
	fadeOutBendingBox->setValue(0.0);
	fadeOutStrengthBox->setValue(0.5);
}

void AudioClipEditDialog::fade_curve_added()
{
    if (m_clip->has_fade_in()) {
		fadein_length_changed();
		fadein_mode_changed();
		fadein_bending_changed();
		fadein_strength_changed();
		connect(m_clip->get_fade_in(), &TFadeCurve::rangeChanged, this, &AudioClipEditDialog::fadein_length_changed);
		connect(m_clip->get_fade_in(), &TFadeCurve::modeChanged, this, &AudioClipEditDialog::fadein_mode_changed);
		connect(m_clip->get_fade_in(), &TFadeCurve::bendValueChanged, this, &AudioClipEditDialog::fadein_bending_changed);
		connect(m_clip->get_fade_in(), &TFadeCurve::strengthValueChanged, this, &AudioClipEditDialog::fadein_strength_changed);
	}
    if (m_clip->has_fade_out()) {
		fadeout_length_changed();
		fadeout_mode_changed();
		fadeout_bending_changed();
		fadeout_strength_changed();
		connect(m_clip->get_fade_out(), &TFadeCurve::rangeChanged, this, &AudioClipEditDialog::fadeout_length_changed);
		connect(m_clip->get_fade_out(), &TFadeCurve::modeChanged, this, &AudioClipEditDialog::fadeout_mode_changed);
		connect(m_clip->get_fade_out(), &TFadeCurve::bendValueChanged, this, &AudioClipEditDialog::fadeout_bending_changed);
		connect(m_clip->get_fade_out(), &TFadeCurve::strengthValueChanged, this, &AudioClipEditDialog::fadeout_strength_changed);
	}
}

void AudioClipEditDialog::update_clip_end()
{
    TTimeRef clipEndLocation = m_clip->get_location_start() + m_clip->get_length();
    QTime clipEndTime = TTimeRef::timeref_to_qtime(clipEndLocation);
	clipEndLineEdit->setText(clipEndTime.toString(TIME_FORMAT));
}

