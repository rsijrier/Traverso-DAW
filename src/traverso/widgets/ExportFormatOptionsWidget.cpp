/**
    Copyright (C) 2008 Remon Sijrier 
 
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

#include "ExportFormatOptionsWidget.h"

#include "AudioDevice.h"
#include "TConfig.h"
#include "TExportSpecification.h"
#include <samplerate.h>

RELAYTOOL_WAVPACK;

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


ExportFormatOptionsWidget::ExportFormatOptionsWidget( QWidget * parent )
	: QWidget(parent)
{
        setupUi(this);

    dataFormatComboBox->addItem("8 bit", SF_FORMAT_PCM_S8);
    dataFormatComboBox->addItem("16 bit", SF_FORMAT_PCM_16);
    dataFormatComboBox->addItem("24 bit", SF_FORMAT_PCM_24);
    dataFormatComboBox->addItem("32 bit", SF_FORMAT_PCM_32);
    dataFormatComboBox->addItem("32 bit FLOAT", SF_FORMAT_FLOAT);
	
	channelComboBox->addItem("Mono", 1);
	channelComboBox->addItem("Stereo", 2);
	
	sampleRateComboBox->addItem("8.000 Hz", 8000);
	sampleRateComboBox->addItem("11.025 Hz", 11025);
	sampleRateComboBox->addItem("22.050 Hz", 22050);
	sampleRateComboBox->addItem("44.100 Hz", 44100);
	sampleRateComboBox->addItem("48.000 Hz", 48000);
	sampleRateComboBox->addItem("88.200 Hz", 88200);
	sampleRateComboBox->addItem("96.000 Hz", 96000);
	
    resampleQualityComboBox->addItem(tr("Best"), SRC_SINC_BEST_QUALITY);
    resampleQualityComboBox->addItem(tr("High"), SRC_SINC_MEDIUM_QUALITY);
    resampleQualityComboBox->addItem(tr("Fastest"), SRC_SINC_FASTEST);
    resampleQualityComboBox->addItem(tr("Zero Order Hold"), SRC_ZERO_ORDER_HOLD);
    resampleQualityComboBox->addItem(tr("Linear"), SRC_LINEAR);
	
	audioTypeComboBox->addItem("WAV", "wav");
	audioTypeComboBox->addItem("AIFF", "aiff");
    audioTypeComboBox->addItem("FLAC", "flac");
    audioTypeComboBox->addItem("MP3", "mp3");
    audioTypeComboBox->addItem("OGG", "ogg");
    if (libwavpack_is_present) {
        audioTypeComboBox->addItem("WAVPACK", "wavpack");
    }

	channelComboBox->setCurrentIndex(channelComboBox->findData(2));
	
	int rateIndex = sampleRateComboBox->findData(audiodevice().get_sample_rate());
	sampleRateComboBox->setCurrentIndex(rateIndex >= 0 ? rateIndex : 3);
	
	connect(audioTypeComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(audio_type_changed(int)));
	
	QString option;
	int index;
	bool checked;
	
	// Mp3 Options Setup
	mp3MethodComboBox->addItem("Constant Bitrate", "cbr");
	mp3MethodComboBox->addItem("Average Bitrate", "abr");
	mp3MethodComboBox->addItem("Variable Bitrate", "vbr-new");
	
	mp3MinBitrateComboBox->addItem("32 Kbps - recommended", "32");
	mp3MinBitrateComboBox->addItem("64 Kbps", "64");
	mp3MinBitrateComboBox->addItem("96 Kbps", "96");
	mp3MinBitrateComboBox->addItem("128 Kbps", "128");
	mp3MinBitrateComboBox->addItem("160 Kbps", "160");
	mp3MinBitrateComboBox->addItem("192 Kbps", "192");
	mp3MinBitrateComboBox->addItem("256 Kbps", "256");
	mp3MinBitrateComboBox->addItem("320 Kbps", "320");
	
	mp3MaxBitrateComboBox->addItem("32 Kbps", "32");
	mp3MaxBitrateComboBox->addItem("64 Kbps", "64");
	mp3MaxBitrateComboBox->addItem("96 Kbps", "96");
	mp3MaxBitrateComboBox->addItem("128 Kbps", "128");
	mp3MaxBitrateComboBox->addItem("160 Kbps", "160");
	mp3MaxBitrateComboBox->addItem("192 Kbps", "192");
	mp3MaxBitrateComboBox->addItem("256 Kbps", "256");
	mp3MaxBitrateComboBox->addItem("320 Kbps", "320");
	
	// First set to VBR, so that if we default to something else, it will trigger mp3_method_changed()
	index = mp3MethodComboBox->findData("vbr-new");
	mp3MethodComboBox->setCurrentIndex(index >=0 ? index : 0);
	connect(mp3MethodComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(mp3_method_changed(int)));
	
	option = config().get_property("ExportFormatOptionsWidget", "mp3MethodComboBox", "vbr-new").toString();
	index = mp3MethodComboBox->findData(option);
	mp3MethodComboBox->setCurrentIndex(index >=0 ? index : 0);
	option = config().get_property("ExportFormatOptionsWidget", "mp3MinBitrateComboBox", "32").toString();
	index = mp3MinBitrateComboBox->findData(option);
	mp3MinBitrateComboBox->setCurrentIndex(index >=0 ? index : 0);
	option = config().get_property("ExportFormatOptionsWidget", "mp3MaxBitrateComboBox", "192").toString();
	index = mp3MaxBitrateComboBox->findData(option);
	mp3MaxBitrateComboBox->setCurrentIndex(index >=0 ? index : 0);
	
	mp3OptionsGroupBox->hide();
	
	
	// Ogg Options Setup
	oggMethodComboBox->addItem("Constant Bitrate", "manual");
	oggMethodComboBox->addItem("Variable Bitrate", "vbr");
	
	oggBitrateComboBox->addItem("45 Kbps", "45");
	oggBitrateComboBox->addItem("64 Kbps", "64");
	oggBitrateComboBox->addItem("96 Kbps", "96");
	oggBitrateComboBox->addItem("112 Kbps", "112");
	oggBitrateComboBox->addItem("128 Kbps", "128");
	oggBitrateComboBox->addItem("160 Kbps", "160");
	oggBitrateComboBox->addItem("192 Kbps", "192");
	oggBitrateComboBox->addItem("224 Kbps", "224");
	oggBitrateComboBox->addItem("256 Kbps", "256");
	oggBitrateComboBox->addItem("320 Kbps", "320");
	oggBitrateComboBox->addItem("400 Kbps", "400");
	
	// First set to VBR, so that if we default to something else, it will trigger ogg_method_changed()
	index = oggMethodComboBox->findData("vbr");
	oggMethodComboBox->setCurrentIndex(index >=0 ? index : 0);
	connect(oggMethodComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(ogg_method_changed(int)));
	
	option = config().get_property("ExportFormatOptionsWidget", "oggMethodComboBox", "vbr").toString();
	index = oggMethodComboBox->findData(option);
	oggMethodComboBox->setCurrentIndex(index >=0 ? index : 0);
	ogg_method_changed(index >=0 ? index : 0);
	option = config().get_property("ExportFormatOptionsWidget", "oggBitrateComboBox", "160").toString();
	index = oggBitrateComboBox->findData(option);
	oggBitrateComboBox->setCurrentIndex(index >= 0 ? index : 0);
	
	oggOptionsGroupBox->hide();
	
	
	// WavPack option
	wacpackGroupBox->hide();
	wavpackCompressionComboBox->addItem("Very high", "very_high");
	wavpackCompressionComboBox->addItem("High", "high");
	wavpackCompressionComboBox->addItem("Fast", "fast");
	
	option = config().get_property("ExportFormatOptionsWidget", "wavpackCompressionComboBox", "very_high").toString();
	index = wavpackCompressionComboBox->findData(option);
	wavpackCompressionComboBox->setCurrentIndex(index >= 0 ? index : 0);
	checked = config().get_property("ExportFormatOptionsWidget", "skipWVXCheckBox", "false").toBool();
	skipWVXCheckBox->setChecked(checked);

	
	option = config().get_property("ExportFormatOptionsWidget", "audioTypeComboBox", "wav").toString();
	index = audioTypeComboBox->findData(option);
	audioTypeComboBox->setCurrentIndex(index >= 0 ? index : 0);
	
	checked = config().get_property("ExportFormatOptionsWidget", "normalizeCheckBox", "false").toBool();
	normalizeCheckBox->setChecked(checked);
	
	index = config().get_property("ExportFormatOptionsWidget", "resampleQualityComboBox", "1").toInt();
	index = resampleQualityComboBox->findData(index);
	resampleQualityComboBox->setCurrentIndex(index >= 0 ? index : 1);

    bool ok;
    int bitDepth = config().get_property("ExportFormatOptionsWidget", "fileFormatComboBox", SF_FORMAT_PCM_16).toInt(&ok);
    if (ok) {
        index = dataFormatComboBox->findData(bitDepth);
        dataFormatComboBox->setCurrentIndex(index >= 0 ? index : 0);
    } else {
        dataFormatComboBox->setCurrentIndex(2);
    }
}


ExportFormatOptionsWidget::~ ExportFormatOptionsWidget( )
{
	config().set_property("ExportDialog", "mp3MethodComboBox", mp3MethodComboBox->itemData(mp3MethodComboBox->currentIndex()).toString());
	config().set_property("ExportDialog", "mp3MinBitrateComboBox", mp3MinBitrateComboBox->itemData(mp3MinBitrateComboBox->currentIndex()).toString());
	config().set_property("ExportDialog", "mp3MaxBitrateComboBox", mp3MaxBitrateComboBox->itemData(mp3MaxBitrateComboBox->currentIndex()).toString());
	config().set_property("ExportDialog", "oggMethodComboBox", oggMethodComboBox->itemData(oggMethodComboBox->currentIndex()).toString());
	config().set_property("ExportDialog", "oggBitrateComboBox", oggBitrateComboBox->itemData(oggBitrateComboBox->currentIndex()).toString());
	config().set_property("ExportDialog", "wavpackCompressionComboBox", wavpackCompressionComboBox->itemData(wavpackCompressionComboBox->currentIndex()).toString());
	config().set_property("ExportDialog", "audioTypeComboBox", audioTypeComboBox->itemData(audioTypeComboBox->currentIndex()).toString());
	config().set_property("ExportDialog", "normalizeCheckBox", normalizeCheckBox->isChecked());
	config().set_property("ExportDialog", "skipWVXCheckBox", skipWVXCheckBox->isChecked());
	config().set_property("ExportDialog", "resampleQualityComboBox", resampleQualityComboBox->itemData(resampleQualityComboBox->currentIndex()).toString());
    config().set_property("ExportDialog", "fileFormatComboBox", dataFormatComboBox->itemData(dataFormatComboBox->currentIndex()).toInt());
}


void ExportFormatOptionsWidget::audio_type_changed(int index)
{
	QString newType = audioTypeComboBox->itemData(index).toString();
	
	if (newType == "mp3") {
		oggOptionsGroupBox->hide();
		wacpackGroupBox->hide();
		mp3OptionsGroupBox->show();
	}
	else if (newType == "ogg") {
		mp3OptionsGroupBox->hide();
		wacpackGroupBox->hide();
		oggOptionsGroupBox->show();
	}
	else if (newType == "wavpack") {
		mp3OptionsGroupBox->hide();
		oggOptionsGroupBox->hide();
		wacpackGroupBox->show();
	}
	else {
		mp3OptionsGroupBox->hide();
		wacpackGroupBox->hide();
		oggOptionsGroupBox->hide();
	}
	
	if (newType == "mp3" || newType == "ogg" || newType == "flac") {
        dataFormatComboBox->setCurrentIndex(dataFormatComboBox->findData(SF_FORMAT_PCM_16));
        dataFormatComboBox->setDisabled(true);
	}
	else {
        dataFormatComboBox->setEnabled(true);
	}
}


void ExportFormatOptionsWidget::mp3_method_changed(int index)
{
	QString method = mp3MethodComboBox->itemData(index).toString();
	
	if (method == "cbr") {
		mp3MinBitrateComboBox->hide();
		mp3MinBitrateLabel->hide();
		mp3MaxBitrateLabel->setText(tr("Bitrate"));
	}
	else if (method == "abr") {
		mp3MinBitrateComboBox->hide();
		mp3MinBitrateLabel->hide();
		mp3MaxBitrateLabel->setText(tr("Average Bitrate"));
	}
	else {
// 		VBR new or VBR old
		mp3MinBitrateComboBox->show();
		mp3MinBitrateLabel->show();
		mp3MaxBitrateLabel->setText(tr("Maximum Bitrate"));
	}
}


void ExportFormatOptionsWidget::ogg_method_changed(int index)
{
	QString method = oggMethodComboBox->itemData(index).toString();
	
	if (method == "manual") {
		oggQualitySlider->hide();
		oggQualityLabel->hide();
		oggBitrateComboBox->show();
		oggBitrateLabel->show();
	}
	else {
		// VBR
		oggBitrateComboBox->hide();
		oggBitrateLabel->hide();
		oggQualitySlider->show();
		oggQualityLabel->show();
	}
}

void ExportFormatOptionsWidget::get_format_options(TExportSpecification * spec)
{
	QString audioType = audioTypeComboBox->itemData(audioTypeComboBox->currentIndex()).toString();
	if (audioType == "wav") {
        spec->set_file_format(SF_FORMAT_WAV);
	}
	else if (audioType == "aiff") {
        spec->set_file_format(SF_FORMAT_AIFF);
    }
	else if (audioType == "flac") {
        spec->set_file_format(SF_FORMAT_FLAC);
    }
	else if (audioType == "wavpack") {
        spec->set_writer_type("wavpack");
		spec->extraFormat["quality"] = wavpackCompressionComboBox->itemData(wavpackCompressionComboBox->currentIndex()).toString();
		spec->extraFormat["skip_wvx"] = skipWVXCheckBox->isChecked() ? "true" : "false";
	}
	else if (audioType == "mp3") {
        spec->set_file_format(SF_FORMAT_MPEG);
        spec->extraFormat["method"] = mp3MethodComboBox->itemData(mp3MethodComboBox->currentIndex()).toString();
		spec->extraFormat["minBitrate"] = mp3MinBitrateComboBox->itemData(mp3MinBitrateComboBox->currentIndex()).toString();
		spec->extraFormat["maxBitrate"] = mp3MaxBitrateComboBox->itemData(mp3MaxBitrateComboBox->currentIndex()).toString();
		spec->extraFormat["quality"] = QString::number(mp3QualitySlider->value());
	}
	else if (audioType == "ogg") {
        spec->set_file_format(SF_FORMAT_OGG);
        spec->extraFormat["mode"] = oggMethodComboBox->itemData(oggMethodComboBox->currentIndex()).toString();
		if (spec->extraFormat["mode"] == "manual") {
			spec->extraFormat["bitrateNominal"] = oggBitrateComboBox->itemData(oggBitrateComboBox->currentIndex()).toString();
			spec->extraFormat["bitrateUpper"] = oggBitrateComboBox->itemData(oggBitrateComboBox->currentIndex()).toString();
		}
		else {
			spec->extraFormat["vbrQuality"] = QString::number(oggQualitySlider->value());
		}
	}

    spec->set_data_format(dataFormatComboBox->itemData(dataFormatComboBox->currentIndex()).toInt());
    spec->set_channel_count(channelComboBox->itemData(channelComboBox->currentIndex()).toUInt());
    spec->set_sample_rate(sampleRateComboBox->itemData(sampleRateComboBox->currentIndex()).toUInt());
    spec->set_sample_rate_conversion_quality(resampleQualityComboBox->itemData(resampleQualityComboBox->currentIndex()).toInt());

	//TODO Make a ComboBox for this one too!
    spec->set_dither_type(GDitherTri);
}
