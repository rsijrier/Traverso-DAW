/**
    Copyright (C) 2008 - 2024 Remon Sijrier
 
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

#include "TAudioDevice.h"
#include "TConfig.h"
#include "TExportSpecification.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TAudioResampler.h"
#include "TResamplerSettings.h"


ExportFormatOptionsWidget::ExportFormatOptionsWidget( QWidget * parent )
	: QWidget(parent)
{
	setupUi(this);

    dataFormatComboBox->addItem(tr("8 bit PCM"),   QVariant::fromValue(TraversoDAW::DataFormat::PCM_S8));
    dataFormatComboBox->addItem(tr("16 bit PCM"),  QVariant::fromValue(TraversoDAW::DataFormat::PCM_16));
    dataFormatComboBox->addItem(tr("24 bit PCM"),  QVariant::fromValue(TraversoDAW::DataFormat::PCM_24));
    dataFormatComboBox->addItem(tr("32 bit PCM"),  QVariant::fromValue(TraversoDAW::DataFormat::PCM_32));
    dataFormatComboBox->addItem(tr("32 bit Float"), QVariant::fromValue(TraversoDAW::DataFormat::FLOAT));

	channelComboBox->addItem("Mono", 1);
	channelComboBox->addItem("Stereo", 2);

    QLocale local;
    for (uint sampleRate : TAudioDeviceSetup::get_sample_rates_list()) {
        sampleRateComboBox->addItem(local.toString(sampleRate), sampleRate);
    }

    // The qualities of the resampler in use
    m_resampler = configured_resampler_backend();
    for (int quality : TAudioResampler::qualities(m_resampler)) {
        resampleQualityComboBox->addItem(TAudioResampler::quality_name(m_resampler, quality), quality);
    }

    resampleQualityComboBox->setToolTipDuration(4000);
    connect(resampleQualityComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        resampleQualityComboBox->setToolTip(TAudioResampler::quality_description(m_resampler, resampleQualityComboBox->currentData().toInt()));
    });

	audioTypeComboBox->addItem("WAV", "wav");
	audioTypeComboBox->addItem("AIFF", "aiff");
    audioTypeComboBox->addItem("FLAC", "flac");
	audioTypeComboBox->addItem("MP3", "mp3");
#if defined M4A_ENCODE_SUPPORT
	audioTypeComboBox->addItem("M4A", "m4a");
#endif
    audioTypeComboBox->addItem("OGG", "ogg");
	audioTypeComboBox->addItem("WAVPACK", "wavpack");
	
	channelComboBox->setCurrentIndex(channelComboBox->findData(2));
	
	// The project's rate (no project: the device's)
	const TProject* project = pm().get_project();
	int rateIndex = sampleRateComboBox->findData(project ? project->project_rate() : audiodevice().get_sample_rate());
	sampleRateComboBox->setCurrentIndex(rateIndex >= 0 ? rateIndex : 3);
	
	connect(audioTypeComboBox, &QComboBox::currentIndexChanged, this, &ExportFormatOptionsWidget::audio_type_changed);
	
	QString option;
	int index;
	bool checked;
	
	// Mp3 Options Setup
	mp3MethodComboBox->addItem("Constant Bitrate", "cbr");
	mp3MethodComboBox->addItem("Average Bitrate", "abr");
    mp3MethodComboBox->addItem("Variable Bitrate", "vbr");
	
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
    index = mp3MethodComboBox->findData("vbr");
	mp3MethodComboBox->setCurrentIndex(index >=0 ? index : 0);
	connect(mp3MethodComboBox, &QComboBox::currentIndexChanged, this, &ExportFormatOptionsWidget::mp3_method_changed);
	
    option = config().get_property("ExportFormatOptionsWidget", "mp3MethodComboBox", "vbr").toString();
	index = mp3MethodComboBox->findData(option);
	mp3MethodComboBox->setCurrentIndex(index >=0 ? index : 0);
	option = config().get_property("ExportFormatOptionsWidget", "mp3MinBitrateComboBox", "32").toString();
	index = mp3MinBitrateComboBox->findData(option);
	mp3MinBitrateComboBox->setCurrentIndex(index >=0 ? index : 0);
	option = config().get_property("ExportFormatOptionsWidget", "mp3MaxBitrateComboBox", "192").toString();
	index = mp3MaxBitrateComboBox->findData(option);
	mp3MaxBitrateComboBox->setCurrentIndex(index >=0 ? index : 0);
	
	mp3OptionsGroupBox->hide();
	
	
	// M4A Options Setup
	m4aBitrateComboBox->addItem("64 Kbps", "64");
	m4aBitrateComboBox->addItem("96 Kbps", "96");
	m4aBitrateComboBox->addItem("128 Kbps", "128");
	m4aBitrateComboBox->addItem("160 Kbps", "160");
	m4aBitrateComboBox->addItem("192 Kbps - recommended", "192");
	m4aBitrateComboBox->addItem("224 Kbps", "224");
	m4aBitrateComboBox->addItem("256 Kbps", "256");
	m4aBitrateComboBox->addItem("320 Kbps", "320");

	option = config().get_property("ExportFormatOptionsWidget", "m4aBitrateComboBox", "192").toString();
	index = m4aBitrateComboBox->findData(option);
	m4aBitrateComboBox->setCurrentIndex(index >= 0 ? index : 4);

	m4aOptionsGroupBox->hide();
	
	
	// Ogg Options Setup
    oggMethodComboBox->addItem("Constant Bitrate", "cbr");
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
	connect(oggMethodComboBox, &QComboBox::currentIndexChanged, this, &ExportFormatOptionsWidget::ogg_method_changed);
	
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
	
    // The backend's own number (resampleQualityComboBox had libsamplerate's)
    const int defaultQuality = TAudioResampler::default_quality(m_resampler);
    index = resampleQualityComboBox->findData(config().get_property("ExportFormatOptionsWidget", "resampleQuality", defaultQuality).toInt());
    resampleQualityComboBox->setCurrentIndex(index >= 0 ? index : resampleQualityComboBox->findData(defaultQuality));

    bool ok;
    int bitDepth = config().get_property("ExportFormatOptionsWidget", "fileFormatComboBox", QVariant::fromValue(TraversoDAW::DataFormat::PCM_16)).toInt(&ok);
    if (ok) {
        index = dataFormatComboBox->findData(bitDepth);
        dataFormatComboBox->setCurrentIndex(index >= 0 ? index : 0);
    } else {
        dataFormatComboBox->setCurrentIndex(2);
    }
}


ExportFormatOptionsWidget::~ ExportFormatOptionsWidget( )
{
    config().set_property("ExportFormatOptionsWidget", "mp3MethodComboBox", mp3MethodComboBox->itemData(mp3MethodComboBox->currentIndex()).toString());
    config().set_property("ExportFormatOptionsWidget", "mp3MinBitrateComboBox", mp3MinBitrateComboBox->itemData(mp3MinBitrateComboBox->currentIndex()).toString());
    config().set_property("ExportFormatOptionsWidget", "mp3MaxBitrateComboBox", mp3MaxBitrateComboBox->itemData(mp3MaxBitrateComboBox->currentIndex()).toString());
	config().set_property("ExportDialog", "m4aBitrateComboBox", m4aBitrateComboBox->itemData(m4aBitrateComboBox->currentIndex()).toString());
	config().set_property("ExportFormatOptionsWidget", "oggMethodComboBox", oggMethodComboBox->itemData(oggMethodComboBox->currentIndex()).toString());
    config().set_property("ExportFormatOptionsWidget", "oggBitrateComboBox", oggBitrateComboBox->itemData(oggBitrateComboBox->currentIndex()).toString());
    config().set_property("ExportFormatOptionsWidget", "wavpackCompressionComboBox", wavpackCompressionComboBox->itemData(wavpackCompressionComboBox->currentIndex()).toString());
    config().set_property("ExportFormatOptionsWidget", "audioTypeComboBox", audioTypeComboBox->itemData(audioTypeComboBox->currentIndex()).toString());
    config().set_property("ExportFormatOptionsWidget", "normalizeCheckBox", normalizeCheckBox->isChecked());
    config().set_property("ExportFormatOptionsWidget", "skipWVXCheckBox", skipWVXCheckBox->isChecked());
    config().set_property("ExportFormatOptionsWidget", "resampleQuality", resampleQualityComboBox->currentData().toInt());
    config().set_property("ExportFormatOptionsWidget", "fileFormatComboBox", dataFormatComboBox->itemData(dataFormatComboBox->currentIndex()).toInt());
}


void ExportFormatOptionsWidget::audio_type_changed(int index)
{
	QString newType = audioTypeComboBox->itemData(index).toString();
	
	if (newType == "mp3") {
		oggOptionsGroupBox->hide();
		wacpackGroupBox->hide();
		mp3OptionsGroupBox->show();
		m4aOptionsGroupBox->hide();
	}
	else if (newType == "ogg") {
		mp3OptionsGroupBox->hide();
		wacpackGroupBox->hide();
		oggOptionsGroupBox->show();
		m4aOptionsGroupBox->hide();
	}
	else if (newType == "wavpack") {
		mp3OptionsGroupBox->hide();
		oggOptionsGroupBox->hide();
		wacpackGroupBox->show();
		m4aOptionsGroupBox->hide();
	}
	else if (newType == "m4a") {
		mp3OptionsGroupBox->hide();
		oggOptionsGroupBox->hide();
		wacpackGroupBox->hide();
		m4aOptionsGroupBox->show();
	}
	else {
		mp3OptionsGroupBox->hide();
		wacpackGroupBox->hide();
		oggOptionsGroupBox->hide();
		m4aOptionsGroupBox->hide();
	}
	
	if (newType == "mp3" || newType == "ogg" || newType == "flac" || newType == "m4a") {
        dataFormatComboBox->setCurrentIndex(dataFormatComboBox->findData(QVariant::fromValue(TraversoDAW::DataFormat::PCM_16)));
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
	
    if (method == "cbr") {
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

void ExportFormatOptionsWidget::get_format_options(TExportSpecification* spec)
{
    Q_ASSERT(spec);

    QVariant audioTypeData = audioTypeComboBox->itemData(audioTypeComboBox->currentIndex());
    TraversoDAW::FileFormat fileFormat = TraversoDAW::FileFormat::RAW;

    if (audioTypeData.isValid()) {
        fileFormat = qvariant_cast<TraversoDAW::FileFormat>(audioTypeData);
    }

    spec->set_file_format(fileFormat);

    spec->clear_extra_formats();

    switch (fileFormat) {
    case TraversoDAW::FileFormat::WAV:
    case TraversoDAW::FileFormat::AIFF:
    case TraversoDAW::FileFormat::W64:
    case TraversoDAW::FileFormat::FLAC:
    case TraversoDAW::FileFormat::WAVPACK:
        spec->add_extra_format(QStringLiteral("quality"), wavpackCompressionComboBox->itemData(wavpackCompressionComboBox->currentIndex()).toString());
        spec->add_extra_format(QStringLiteral("skip_wvx"), skipWVXCheckBox->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        break;

    case TraversoDAW::FileFormat::MP3:
        spec->add_extra_format(QStringLiteral("method"), mp3MethodComboBox->itemData(mp3MethodComboBox->currentIndex()).toString());
        spec->add_extra_format(QStringLiteral("minBitrate"), mp3MinBitrateComboBox->itemData(mp3MinBitrateComboBox->currentIndex()).toString());
        spec->add_extra_format(QStringLiteral("maxBitrate"), mp3MaxBitrateComboBox->itemData(mp3MaxBitrateComboBox->currentIndex()).toString());
        spec->add_extra_format(QStringLiteral("quality"), QString::number(mp3QualitySlider->value()));
        break;

    case TraversoDAW::FileFormat::OGG:
    {
        QString oggMode = oggMethodComboBox->itemData(oggMethodComboBox->currentIndex()).toString();
        spec->add_extra_format(QStringLiteral("mode"), oggMode);

        if (oggMode == QStringLiteral("cbr")) {
            spec->add_extra_format(QStringLiteral("bitrateNominal"), oggBitrateComboBox->itemData(oggBitrateComboBox->currentIndex()).toString());
            spec->add_extra_format(QStringLiteral("bitrateUpper"), oggBitrateComboBox->itemData(oggBitrateComboBox->currentIndex()).toString());
        } else {
            spec->add_extra_format(QStringLiteral("vbrQuality"), QString::number(oggQualitySlider->value()));
        }
    }
    break;

    default:
        break;
    }

    QVariant dataFormatData = dataFormatComboBox->itemData(dataFormatComboBox->currentIndex());
    if (dataFormatData.isValid()) {
        spec->set_data_format(qvariant_cast<TraversoDAW::DataFormat>(dataFormatData));
    }

    spec->set_channel_count(channelComboBox->itemData(channelComboBox->currentIndex()).toUInt());
    spec->set_sample_rate(sampleRateComboBox->itemData(sampleRateComboBox->currentIndex()).toUInt());
    spec->set_resampler({m_resampler, qint16(resampleQualityComboBox->currentData().toInt())});

    // TODO Make a ComboBox for this one too!
    spec->set_dither_type(GDitherTri);
}
