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

#include "AudioClip.h"
#include "FadeCurve.h"
#include "ProjectManager.h"
#include "Project.h"
#include "ReadSource.h"
#include "Utils.h"
#include "Mixer.h"
#include "TCommand.h"
#include "AudioClipExternalProcessing.h"
#include "TInputEventDispatcher.h"
#include "AudioDevice.h"

#define TIME_FORMAT "hh:mm:ss.zzz"

AudioClipEditDialog::AudioClipEditDialog(AudioClip* clip, QWidget* parent) 
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
	clip_position_changed();
	
	// detect and set fade params
	fade_curve_added();
	
	connect(clip, SIGNAL(stateChanged()), this, SLOT(clip_state_changed()));
	connect(clip, SIGNAL(positionChanged()), this, SLOT(clip_position_changed()));
	connect(clip, SIGNAL(fadeAdded(FadeCurve*)), this, SLOT(fade_curve_added()));
	
	connect(clipGainSpinBox, SIGNAL(valueChanged(double)), this, SLOT(gain_spinbox_value_changed(double)));
	
	connect(clipStartEdit, SIGNAL(timeChanged(const QTime&)), this, SLOT(clip_start_edit_changed(const QTime&)));
	connect(clipLengthEdit, SIGNAL(timeChanged(const QTime&)), this, SLOT(clip_length_edit_changed(const QTime&)));
	
	connect(fadeInEdit, SIGNAL(timeChanged(const QTime&)), this, SLOT(fadein_edit_changed(const QTime&)));
	connect(fadeInModeBox, SIGNAL(currentIndexChanged(int)), this, SLOT(fadein_mode_edit_changed(int)));
	connect(fadeInBendingBox, SIGNAL(valueChanged(double)), this, SLOT(fadein_bending_edit_changed(double)));
	connect(fadeInStrengthBox, SIGNAL(valueChanged(double)), this, SLOT(fadein_strength_edit_changed(double)));
	connect(fadeInLinearButton, SIGNAL(clicked()), this, SLOT(fadein_linear()));
	connect(fadeInDefaultButton, SIGNAL(clicked()), this, SLOT(fadein_default()));

	connect(fadeOutEdit, SIGNAL(timeChanged(const QTime&)), this, SLOT(fadeout_edit_changed(const QTime&)));
	connect(fadeOutModeBox, SIGNAL(currentIndexChanged(int)), this, SLOT(fadeout_mode_edit_changed(int)));
	connect(fadeOutBendingBox, SIGNAL(valueChanged(double)), this, SLOT(fadeout_bending_edit_changed(double)));
	connect(fadeOutStrengthBox, SIGNAL(valueChanged(double)), this, SLOT(fadeout_strength_edit_changed(double)));
	connect(fadeOutLinearButton, SIGNAL(clicked()), this, SLOT(fadeout_linear()));
	connect(fadeOutDefaultButton, SIGNAL(clicked()), this, SLOT(fadeout_default()));
	
	connect(externalProcessingButton, SIGNAL(clicked()), this, SLOT(external_processing()));
	connect(buttonBox, SIGNAL(accepted()), this, SLOT(save_changes()));
    connect(buttonBox, SIGNAL(rejected()), this, SLOT(cancel_changes()));
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
	
    clipGainSpinBox->setValue(coefficient_to_dB(m_clip->get_gain()));
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

void AudioClipEditDialog::clip_position_changed()
{
	if (locked) return;

	QTime clipLengthTime = timeref_to_qtime(m_clip->get_length());
	clipLengthEdit->setTime(clipLengthTime);
	
	QTime clipStartTime = timeref_to_qtime(m_clip->get_location()->get_start());
	clipStartEdit->setTime(clipStartTime);

	update_clip_end();
}

void AudioClipEditDialog::fadein_length_changed()
{
	if (locked) return;
	
	TTimeRef ref(qint64(m_clip->get_fade_in()->get_range()));
	QTime fadeTime = timeref_to_qtime(ref);
	fadeInEdit->setTime(fadeTime);
}

void AudioClipEditDialog::fadeout_length_changed()
{
	if (locked) return;

	TTimeRef ref(qint64(m_clip->get_fade_out()->get_range()));
	QTime fadeTime = timeref_to_qtime(ref);
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
	double range = double(qtime_to_timeref(time).universal_frame());
	if (range == 0) {
		m_clip->set_fade_in(1);
	} else {
		m_clip->set_fade_in(range);
	}
	locked = false;
}

void AudioClipEditDialog::fadeout_edit_changed(const QTime& time)
{
	if (ied().is_holding()) return;

	locked = true;
	double range = double(qtime_to_timeref(time).universal_frame());
	if (range == 0) {
		m_clip->set_fade_out(1);
	} else {
		m_clip->set_fade_out(range);
	}
	locked = false;
}

void AudioClipEditDialog::clip_length_edit_changed(const QTime& time)
{
	if (ied().is_holding()) return;

	locked = true;
	
	TTimeRef ref = qtime_to_timeref(time);

	if (ref >= m_clip->get_source_length()) {
		ref = m_clip->get_source_length();
		QTime clipLengthTime = timeref_to_qtime(ref);
		clipLengthEdit->setTime(clipLengthTime);
	}

	m_clip->set_right_edge(ref + m_clip->get_location()->get_start());
	update_clip_end();
	locked = false;
}

void AudioClipEditDialog::clip_start_edit_changed(const QTime& time)
{
	if (ied().is_holding()) return;

	locked = true;
	m_clip->set_location_start(qtime_to_timeref(time));
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
	if (!m_clip->get_fade_in()) return;
	locked = true;
	m_clip->get_fade_in()->set_mode(index);
	locked = false;
}

void AudioClipEditDialog::fadeout_mode_edit_changed(int index)
{
	if (!m_clip->get_fade_out()) return;
	locked = true;
	m_clip->get_fade_out()->set_mode(index);
	locked = false;
}

void AudioClipEditDialog::fadein_bending_edit_changed(double value)
{
	if (!m_clip->get_fade_in()) return;
	locked = true;
	m_clip->get_fade_in()->set_bend_factor(value);
	locked = false;
}

void AudioClipEditDialog::fadeout_bending_edit_changed(double value)
{
	if (!m_clip->get_fade_out()) return;
	locked = true;
	m_clip->get_fade_out()->set_bend_factor(value);
	locked = false;
}

void AudioClipEditDialog::fadein_strength_edit_changed(double value)
{
	if (!m_clip->get_fade_in()) return;
	locked = true;
	m_clip->get_fade_in()->set_strength_factor(value);
	locked = false;
}

void AudioClipEditDialog::fadeout_strength_edit_changed(double value)
{
	if (!m_clip->get_fade_out()) return;
	locked = true;
	m_clip->get_fade_out()->set_strength_factor(value);
	locked = false;
}

void AudioClipEditDialog::fadein_linear()
{
	if (!m_clip->get_fade_in()) return;
	fadeInBendingBox->setValue(0.5);
	fadeInStrengthBox->setValue(0.5);
}

void AudioClipEditDialog::fadein_default()
{
	if (!m_clip->get_fade_in()) return;
	fadeInBendingBox->setValue(0.0);
	fadeInStrengthBox->setValue(0.5);
}

void AudioClipEditDialog::fadeout_linear()
{
	if (!m_clip->get_fade_out()) return;
	fadeOutBendingBox->setValue(0.5);
	fadeOutStrengthBox->setValue(0.5);
}

void AudioClipEditDialog::fadeout_default()
{
	if (!m_clip->get_fade_out()) return;
	fadeOutBendingBox->setValue(0.0);
	fadeOutStrengthBox->setValue(0.5);
}

TTimeRef AudioClipEditDialog::qtime_to_timeref(const QTime & time)
{
    TTimeRef ref(time.hour() * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE + time.minute() * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE + time.second() * TTimeRef::UNIVERSAL_SAMPLE_RATE + (time.msec() * TTimeRef::UNIVERSAL_SAMPLE_RATE) / 1000);
	return ref;
}

QTime AudioClipEditDialog::timeref_to_qtime(const TTimeRef& ref)
{
	qint64 remainder;
	int hours, mins, secs, msec;

	qint64 universalframe = ref.universal_frame();
	
    hours = universalframe / (TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    remainder = universalframe - (hours * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    mins = remainder / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    remainder = remainder - (mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
	secs = remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE;
	remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
	msec = remainder * 1000 / TTimeRef::UNIVERSAL_SAMPLE_RATE;

	QTime time(hours, mins, secs, msec);
	return time;
}

void AudioClipEditDialog::fade_curve_added()
{
	if (m_clip->get_fade_in()) {
		fadein_length_changed();
		fadein_mode_changed();
		fadein_bending_changed();
		fadein_strength_changed();
		connect(m_clip->get_fade_in(), SIGNAL(rangeChanged()), this, SLOT(fadein_length_changed()));
		connect(m_clip->get_fade_in(), SIGNAL(modeChanged()), this, SLOT(fadein_mode_changed()));
		connect(m_clip->get_fade_in(), SIGNAL(bendValueChanged()), this, SLOT(fadein_bending_changed()));
		connect(m_clip->get_fade_in(), SIGNAL(strengthValueChanged()), this, SLOT(fadein_strength_changed()));
	}
	if (m_clip->get_fade_out()) {
		fadeout_length_changed();
		fadeout_mode_changed();
		fadeout_bending_changed();
		fadeout_strength_changed();
		connect(m_clip->get_fade_out(), SIGNAL(rangeChanged()), this, SLOT(fadeout_length_changed()));
		connect(m_clip->get_fade_out(), SIGNAL(modeChanged()), this, SLOT(fadeout_mode_changed()));
		connect(m_clip->get_fade_out(), SIGNAL(bendValueChanged()), this, SLOT(fadeout_bending_changed()));
		connect(m_clip->get_fade_out(), SIGNAL(strengthValueChanged()), this, SLOT(fadeout_strength_changed()));
	}
}

void AudioClipEditDialog::update_clip_end()
{
	TTimeRef clipEndLocation = m_clip->get_location()->get_start() + m_clip->get_length();
	QTime clipEndTime = timeref_to_qtime(clipEndLocation);
	clipEndLineEdit->setText(clipEndTime.toString(TIME_FORMAT));
}

