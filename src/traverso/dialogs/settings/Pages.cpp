/*
Copyright (C) 2007-2009 Remon Sijrier

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

#include <QTranslator>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QStyle>
#include <QStyleFactory>

#include "Pages.h"
#include "TAudioResampler.h"
#include "TResamplerSettings.h"
#include "dialogs/ThemeModifierDialog.h"
#include "TAudioDevice.h"
#if defined (ALSA_SUPPORT)
#include "TAlsaDriver.h"
#endif

#if defined (PORTAUDIO_SUPPORT)
#include "TPortAudioDriver.h"
#endif

#include "TConfig.h"
#include <Utils.h>
#include <TThemer.h>
#include "TContextPointer.h"
#include "TMainWindow.h"
#include "TShortCutManager.h"
#include <QDomDocument>
#include <QUrl>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>


/****************************************/
/*            AudioDriver               */
/****************************************/



// The translations built in, as qm files
static QStringList find_qm_files()
{
	QDir dir(":/translations");
	QStringList fileNames = dir.entryList(QStringList("*.qm"), QDir::Files, QDir::Name);
	QMutableStringListIterator i(fileNames);
	while (i.hasNext()) {
		i.next();
		i.setValue(dir.filePath(i.value()));
	}
	return fileNames;
}

// The name a translation gives its own language
static QString language_name_from_qm_file(const QString& lang)
{
	QTranslator translator;
	if (translator.load(lang)) {
		return translator.translate("LanguageName", "English", "The name of this Language, e.g. German would be Deutch");
	}
	return QString("Failed to load language name from qm file");
}

AudioDriverConfigPage::AudioDriverConfigPage(QWidget *parent)
    : ConfigPage(parent)
{
    setupUi(this);
    driverInformationTextEdit->setTextInteractionFlags(Qt::NoTextInteraction);

    m_mainLayout = qobject_cast<QVBoxLayout*>(layout());

    QStringList drivers = audiodevice().get_available_drivers();
    for(QString name : drivers) {
        driverCombo->addItem(name);
    }
    QLocale local;
    for (uint sampleRate : TAudioDeviceSetup::get_sample_rates_list()) {
        rateComboBox->addItem(local.toString(sampleRate), sampleRate);
    }


    m_portaudiodrivers = new PaDriverPage(this);
    m_mainLayout->addWidget(m_portaudiodrivers);

    m_alsadevices = new AlsaDevicesPage(this);
    m_alsadevices->layout()->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->addWidget(m_alsadevices);

#if defined (Q_OS_MAC)
    m_coreAudioInputDevice = coreAudioInputDeviceComboBox;
    m_coreAudioOutputDevice = coreAudioOutputDeviceComboBox;
#endif

        connect(driverCombo, &QComboBox::currentTextChanged, this, &AudioDriverConfigPage::driver_combobox_index_changed);
    connect(m_alsadevices->periodsCombo, &QComboBox::currentIndexChanged, this, [this]() {
            update_latency_combobox();
        });
    connect(restartDriverButton, &QPushButton::clicked, this, &AudioDriverConfigPage::restart_driver_button_clicked);
        connect(rateComboBox, &QComboBox::currentIndexChanged, this, [this]() {
        update_latency_combobox();
    });
    connect(&audiodevice(), &TAudioDevice::newDriverSetupMessage, this, &AudioDriverConfigPage::new_driver_setup_message);

#if defined (PORTAUDIO_SUPPORT)
        connect(m_portaudiodrivers->driverCombo, &QComboBox::currentIndexChanged, this, &AudioDriverConfigPage::portaudio_host_api_combobox_index_changed);
#endif

    update_latency_combobox();
    
    load_config();
}

void AudioDriverConfigPage::save_config()
{
    config().set_property("Hardware", "samplerate", rateComboBox->currentData());
    int bufferindex = latencyComboBox->currentIndex();
    int buffersize = 1024;
    auto bufferSizesList = TAudioDeviceSetup::get_buffer_sizes_list();
    if (bufferindex >= 0 && bufferindex < bufferSizesList.size()) {
        buffersize = bufferSizesList.at(bufferindex);
    }
    config().set_property("Hardware", "buffersize", buffersize);

    config().set_property("Hardware", "drivertype", driverCombo->currentText());

    int playback=1, capture=1;
    if(duplexComboBox->currentIndex() == 1) {
        capture = 0;
    }

    if(duplexComboBox->currentIndex() == 2) {
        playback = 0;
    }

    config().set_property("Hardware", "capture", capture);
    config().set_property("Hardware", "playback", playback);

#if defined (Q_OS_MAC)
    config().set_property("Hardware", "coreaudioinput",
                          m_coreAudioInputDevice->currentData());
    config().set_property("Hardware", "coreaudiooutput",
                          m_coreAudioOutputDevice->currentData());
#endif


#if defined (ALSA_SUPPORT)
    int periods = m_alsadevices->periodsCombo->currentText().toInt();
    config().set_property("Hardware", "numberofperiods", periods);
    int index = m_alsadevices->devicesCombo->currentIndex();
    config().set_property("Hardware", "carddevice", m_alsadevices->devicesCombo->itemData(index));
    config().set_property("Hardware", "DitherShape", m_alsadevices->ditherShapeComboBox->currentText());

#endif


#if defined (PORTAUDIO_SUPPORT)
    int paindex = m_portaudiodrivers->driverCombo->currentIndex();
    config().set_property("Hardware", "pahostapi", m_portaudiodrivers->driverCombo->itemData(paindex));
#endif

    config().set_property("Hardware", "jackslave", jackTransportCheckBox->isChecked());
}

void AudioDriverConfigPage::reset_default_config()
{
    config().set_property("Hardware", "samplerate", 44100);
    config().set_property("Hardware", "buffersize", 512);
#if defined (ALSA_SUPPORT)
    config().set_property("Hardware", "drivertype", "ALSA");
        config().set_property("Hardware", "carddevice", "default");
    config().set_property("Hardware", "numberofperiods", 3);
    config().set_property("Hardware", "DitherShape", "None");
#elif defined (JACK_SUPPORT)
    config().set_property("Hardware", "drivertype", "Jack");
#elif defined (PIPEWIRE_SUPPORT)
    config().set_property("Hardware", "drivertype", "PipeWire");
#else
    config().set_property("Hardware", "drivertype", "Dummy");
#endif

#if defined (PORTAUDIO_SUPPORT)
#if defined (Q_OS_LINUX)
    config().set_property("Hardware", "pahostapi", "alsa");
#endif
#if defined (Q_OS_MAC)
    config().set_property("Hardware", "pahostapi", "coreaudio");
#endif
#if defined (Q_OS_WIN)
    config().set_property("Hardware", "pahostapi", "wmme");
#endif
#endif //end PORTAUDIO_SUPPORT

    config().set_property("Hardware", "capture", 1);
    config().set_property("Hardware", "playback", 1);

    config().set_property("Hardware", "jackslave", false);

    load_config();
}

void AudioDriverConfigPage::load_config( )
{
    int samplerate = config().get_property("Hardware", "samplerate", 44100).toInt();
    int buffersize = config().get_property("Hardware", "buffersize", 512).toInt();
#if defined (Q_OS_LINUX)
    QString driverType = config().get_property("Hardware", "drivertype", "ALSA").toString();
#else
    QString driverType = config().get_property("Hardware", "drivertype", "PortAudio").toString();
#endif
    bool capture = config().get_property("Hardware", "capture", 1).toInt();
    bool playback = config().get_property("Hardware", "playback", 1).toInt();


    int driverTypeIndex = driverCombo->findText(driverType);
    if (driverTypeIndex >= 0) {
        driverCombo->setCurrentIndex(driverTypeIndex);
    }

    driver_combobox_index_changed(driverType);

#if defined (Q_OS_MAC)
    m_coreAudioInputDevice->clear();
    m_coreAudioOutputDevice->clear();
    m_coreAudioInputDevice->addItem(tr("System default"), QStringLiteral("default"));
    m_coreAudioOutputDevice->addItem(tr("System default"), QStringLiteral("default"));
    for (const QString& device : TCoreAudioDriver::devices_info(true)) {
        const QStringList fields = device.split(QStringLiteral("###"));
        m_coreAudioInputDevice->addItem(fields.at(0), fields.at(1));
    }
    for (const QString& device : TCoreAudioDriver::devices_info(false)) {
        const QStringList fields = device.split(QStringLiteral("###"));
        m_coreAudioOutputDevice->addItem(fields.at(0), fields.at(1));
    }
    int coreAudioIndex = m_coreAudioInputDevice->findData(
        config().get_property("Hardware", "coreaudioinput", "default"));
    if (coreAudioIndex >= 0) m_coreAudioInputDevice->setCurrentIndex(coreAudioIndex);
    coreAudioIndex = m_coreAudioOutputDevice->findData(
        config().get_property("Hardware", "coreaudiooutput", "default"));
    if (coreAudioIndex >= 0) m_coreAudioOutputDevice->setCurrentIndex(coreAudioIndex);
#endif

    int buffersizeIndex = TAudioDeviceSetup::get_buffer_sizes_list().indexOf(buffersize);
    int samplerateIndex = rateComboBox->findData(samplerate);

    rateComboBox->setCurrentIndex(samplerateIndex);
    latencyComboBox->setCurrentIndex(buffersizeIndex);


    if (capture && playback) {
        duplexComboBox->setCurrentIndex(0);
    } else if (playback) {
        duplexComboBox->setCurrentIndex(1);
    } else {
        duplexComboBox->setCurrentIndex(2);
    }

    int index;
    
    (void)index;

#if defined (ALSA_SUPPORT)
    m_alsadevices->devicesCombo->clear();
    int periodsIndex = config().get_property("Hardware", "numberofperiods", 3).toInt();
    QString ditherShape = config().get_property("Hardware", "DitherShape", "None").toString();
    m_alsadevices->periodsCombo->setCurrentIndex(periodsIndex - 2);

    int shapeIndex = m_alsadevices->ditherShapeComboBox->findText(ditherShape);
    if (shapeIndex >=0) {
        m_alsadevices->ditherShapeComboBox->setCurrentIndex(shapeIndex);
    }

    // Always add the 'default' device
    m_alsadevices->devicesCombo->addItem(tr("System default"), "default");

    // The cards ALSA knows
    QString name, longName;
    for (int card : TAlsaDriver::alsa_card_numbers()) {
        name = TAlsaDriver::alsa_device_name(card);
        longName = TAlsaDriver::alsa_device_longname(card);
        if (name != "") {
                m_alsadevices->devicesCombo->addItem(name + ", " + longName + "", name);
        }
    }

        QString defaultdevice =  config().get_property("Hardware", "carddevice", "default").toString();
    index = m_alsadevices->devicesCombo->findData(defaultdevice);
    if (index >= 0) {
        m_alsadevices->devicesCombo->setCurrentIndex(index);
    }
#endif

#if defined (PORTAUDIO_SUPPORT)
    m_portaudiodrivers->driverCombo->clear();
    QString defaulthostapi = "";

#if defined (Q_OS_LINUX)
    m_portaudiodrivers->driverCombo->addItem("ALSA", "alsa");
    m_portaudiodrivers->driverCombo->addItem("Jack", "jack");
    m_portaudiodrivers->driverCombo->addItem("OSS", "oss");
    defaulthostapi = "jack";
#endif

#if defined (Q_OS_MAC)
    m_portaudiodrivers->driverCombo->addItem("Core Audio", "coreaudio");
    m_portaudiodrivers->driverCombo->addItem("Jack", "jack");
    defaulthostapi = "coreaudio";
#endif

#if defined (Q_OS_WIN)
    m_portaudiodrivers->driverCombo->addItem("MME", "wmme");
    m_portaudiodrivers->driverCombo->addItem("Direct Sound", "directsound");
        m_portaudiodrivers->driverCombo->addItem("WASAPI", "wasapi");
        m_portaudiodrivers->driverCombo->addItem("WDMKS", "wdmks");
        m_portaudiodrivers->driverCombo->addItem("ASIO", "asio");
        defaulthostapi = "wmme";
#endif

    QString hostapi = config().get_property("Hardware", "pahostapi", defaulthostapi).toString();
    index = m_portaudiodrivers->driverCombo->findData(hostapi);
    if (index >= 0) {
        m_portaudiodrivers->driverCombo->setCurrentIndex(index);
    }

    update_latency_combobox();

#endif //end PORTAUDIO_SUPPORT

    bool usetransport = config().get_property("Hardware", "jackslave", false).toBool();
    jackTransportCheckBox->setChecked(usetransport);
}


void AudioDriverConfigPage::restart_driver_button_clicked()
{
    driverInformationTextEdit->clear();

    TAudioDeviceSetup audioDeviceSetup = audiodevice().get_device_setup();
    QString driver = driverCombo->currentText();
    audioDeviceSetup.set_sample_rate(rateComboBox->currentData().toInt());
    audioDeviceSetup.set_buffer_size(TAudioDeviceSetup::get_buffer_sizes_list().at(latencyComboBox->currentIndex()));

    audioDeviceSetup.set_playback(true);
    audioDeviceSetup.set_capture(true);
    if(duplexComboBox->currentIndex() == 1) {
        audioDeviceSetup.set_capture(false);
    }

    if(duplexComboBox->currentIndex() == 2) {
        audioDeviceSetup.set_playback(false);
    }


    audioDeviceSetup.set_card_device("");
    audioDeviceSetup.set_dither_shape("None");


#if defined (ALSA_SUPPORT)
    int periods = m_alsadevices->periodsCombo->currentText().toInt();
    audioDeviceSetup.set_dither_shape(m_alsadevices->ditherShapeComboBox->currentText());
    // The AlsaDriver retrieves it's periods number directly from config()
    // So there is no way to use the current selected one, other then
    // setting it now, and restoring it afterwards...
    int currentperiods = config().get_property("Hardware", "numberofperiods", 3).toInt();
    config().set_property("Hardware", "numberofperiods", periods);

    if (driver == "ALSA") {
        int index = m_alsadevices->devicesCombo->currentIndex();
        audioDeviceSetup.set_card_device(m_alsadevices->devicesCombo->itemData(index).toString());
    }
#endif

#if defined (PORTAUDIO_SUPPORT)
    if (driver == "PortAudio") {
        int index = m_portaudiodrivers->driverCombo->currentIndex();
        QString cardDevice = m_portaudiodrivers->driverCombo->itemData(index).toString();
        cardDevice += "::" + m_portaudiodrivers->inputDevicesCombo->currentText();
        cardDevice += "::" + m_portaudiodrivers->outputDevicesCombo->currentText();
        audioDeviceSetup.set_card_device(cardDevice);
    }
#endif

#if defined (Q_OS_MAC)
    if (driver == "CoreAudio") {
        const QString input = m_coreAudioInputDevice->currentData().toString();
        const QString output = m_coreAudioOutputDevice->currentData().toString();
        if (duplexComboBox->currentIndex() == 0) {
            audioDeviceSetup.set_card_device(
                QString::fromLatin1(QUrl::toPercentEncoding(input)) + "::" +
                QString::fromLatin1(QUrl::toPercentEncoding(output)));
        } else if (duplexComboBox->currentIndex() == 1) {
            audioDeviceSetup.set_card_device(output);
        } else {
            audioDeviceSetup.set_card_device(input);
        }
    }
#endif

    audioDeviceSetup.set_driver_type(driver);
    audiodevice().set_parameters(audioDeviceSetup);

#if defined (ALSA_SUPPORT)
    config().set_property("Hardware", "numberofperiods", currentperiods);
#endif

    config().set_property("Hardware", "jackslave", jackTransportCheckBox->isChecked());
}



void AudioDriverConfigPage::driver_combobox_index_changed(QString driver)
{
    m_mainLayout->removeWidget(m_alsadevices);
    m_mainLayout->removeWidget(m_portaudiodrivers);
    m_mainLayout->removeWidget(jackGroupBox);

    if (driver == "ALSA") {
        m_alsadevices->show();
        m_mainLayout->insertWidget(m_mainLayout->indexOf(driverConfigGroupBox), m_alsadevices);
    } else {
        m_alsadevices->hide();
        m_mainLayout->removeWidget(m_alsadevices);
    }

    if (driver == "PortAudio") {
        m_portaudiodrivers->show();
        m_mainLayout->insertWidget(m_mainLayout->indexOf(driverConfigGroupBox), m_portaudiodrivers);
    } else {
        m_portaudiodrivers->hide();
        m_mainLayout->removeWidget(m_portaudiodrivers);
    }

    if (driver == "Jack") {
        jackGroupBox->show();
        m_mainLayout->insertWidget(m_mainLayout->indexOf(driverConfigGroupBox), jackGroupBox);
    } else {
        jackGroupBox->hide();
        m_mainLayout->removeWidget(jackGroupBox);
    }

    if (driver == "CoreAudio") {
        coreAudioDeviceGroupBox->show();
        m_mainLayout->insertWidget(m_mainLayout->indexOf(driverConfigGroupBox), coreAudioDeviceGroupBox);
    } else {
        coreAudioDeviceGroupBox->hide();
        m_mainLayout->removeWidget(coreAudioDeviceGroupBox);
    }

#if defined (Q_OS_MAC)
    const bool coreAudio = driver == "CoreAudio";
    coreAudioDeviceGroupBox->setVisible(coreAudio);
#endif
}

#if defined (PORTAUDIO_SUPPORT)
void AudioDriverConfigPage::portaudio_host_api_combobox_index_changed(int index)
{
        if (m_portaudiodrivers->isHidden()) {
                return;
        }

        QStringList list = TPortAudioDriver::devices_info(m_portaudiodrivers->driverCombo->itemData(index).toString());

        m_portaudiodrivers->inputDevicesCombo->clear();
        m_portaudiodrivers->outputDevicesCombo->clear();

        for (int i=0; i<list.size(); ++i) {
                QStringList deviceInfo = list.at(i).split("###");
                if (deviceInfo.size() > 0) {
                        m_portaudiodrivers->inputDevicesCombo->addItem(deviceInfo.at(0));
                        m_portaudiodrivers->outputDevicesCombo->addItem(deviceInfo.at(0));
                }

                if (deviceInfo.size() > 1 && deviceInfo.at(1) == "defaultInputDevice") {
                        m_portaudiodrivers->inputDevicesCombo->setCurrentIndex(i);
                }

                if (deviceInfo.size() > 1 && deviceInfo.at(1) == "defaultOutputDevice") {
                        m_portaudiodrivers->outputDevicesCombo->setCurrentIndex(i);
                }
        }
}
#endif

void AudioDriverConfigPage::update_latency_combobox( )
{
    latencyComboBox->clear();
    int rate = rateComboBox->currentData().toInt();

    // FIXME
    // This is only valid for alsa driver, we should get it from the actual
    // running driver instead to show the _real_ latency instead of a
    // theoratical one
    // For now default to 2 since this is most likely close (enough) to the reality
    int periods = m_alsadevices->periodsCombo->currentText().toInt();
    for (uint bufferSize : TAudioDeviceSetup::get_buffer_sizes_list()) {
        QString latency = QString::number( ((float)(bufferSize) / rate) * 1000 * periods, 'f', 2);
        latencyComboBox->addItem(latency, bufferSize);
    }

    latencyComboBox->setCurrentIndex(latencyComboBox->findData(audiodevice().get_buffer_size()));
}


void AudioDriverConfigPage::new_driver_setup_message()
{
    driverInformationTextEdit->clear();
    if (driverInformationTextEdit->isHidden()) {
        driverInformationTextEdit->show();
    }

    QStringList stringList;
    auto driverSetupMessages = audiodevice().get_audio_driver_setup_messages();
    for (const TAudioDriverSetupMessage &driverSetupMessage : driverSetupMessages) {
        int severity = driverSetupMessage.severity;
        QString createdOnString = QDateTime::fromMSecsSinceEpoch(driverSetupMessage.createdOn).toString("hh:mm:ss");
        QString message = createdOnString + ": " + driverSetupMessage.driverType + "<br>" + driverSetupMessage.message;

        if (severity == TAudioDevice::DRIVER_SETUP_FAILURE || severity == TAudioDevice::CRITICAL) {
            stringList.prepend("<p class=\"failure\">" + message + "</p>");
        } else if (severity == TAudioDevice::DRIVER_SETUP_WARNING || severity == TAudioDevice::WARNING) {
            stringList.prepend("<p class=\"warning\">" + message + "</p>");
        } else if (severity == TAudioDevice::DRIVER_SETUP_SUCCESS || severity == TAudioDevice::DRIVER_SETUP_INFO || severity == TAudioDevice::INFO) {
            stringList.prepend("<p class=\"success\">" + message + "</p>");
        } else {
            stringList.prepend("<p>" + message + "</p>");
        }

    }

    QString html = QString("<html><head><meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n"
                           "<style type=\"text/css\">\n"
                           "p {font-size: 12px; }\n"
                           ".failure {color: red;}\n"
                           ".warning {color: #FF5F15;}\n"
                           ".success {color: green;}\n"
                           "</style>\n"
                           "</head>\n<body>\n");

    for(const QString &string : stringList) {
        html += string;
    }

    html += "</body>\n</html>";


    driverInformationTextEdit->insertHtml(html);

}


/****************************************/
/*            Appearance                */
/****************************************/


void AppearenceConfigPage::save_config()
{
    config().set_property("Themer", "themepath", themePathLineEdit->text());
        config().set_property("Themer", "currenttheme", themeSelecterCombo->currentText());
    config().set_property("Themer", "coloradjust", colorAdjustBox->value());
    config().set_property("Themer", "style", styleCombo->currentText());
    config().set_property("Themer", "usestylepallet", useStylePalletCheckBox->isChecked());
    config().set_property("Themer", "paintaudiorectified", rectifiedCheckBox->isChecked());
    config().set_property("Themer", "paintstereoaudioasmono", mergedCheckBox->isChecked());
    config().set_property("Themer", "drawdbgrid", dbGridCheckBox->isChecked());
    config().set_property("Themer", "paintwavewithoutline", paintAudioWithOutlineCheckBox->isChecked());
    config().set_property("Themer", "iconsize", iconSizeCombo->currentText());
    config().set_property("Themer", "toolbuttonstyle", toolbarStyleCombo->currentIndex());
    config().set_property("Themer", "supportediconsizes", supportedIconSizes);
    config().set_property("Themer", "transportconsolesize", transportConsoleCombo->currentText());
    config().set_property("Interface", "LanguageFile", languageComboBox->itemData(languageComboBox->currentIndex()));
        config().set_property("Themer", "VUOrientation", trackVUOrientationCheckBox->isChecked() ? Qt::Horizontal : Qt::Vertical);
}

void AppearenceConfigPage::load_config()
{
    QIcon icon = QApplication::style()->standardIcon(QStyle::SP_DirClosedIcon);
    pathSelectButton->setIcon(icon);
    QString themepath = config().get_property("Themer", "themepath",
                   QString(QDir::homePath()).append(".traverso/themes")).toString();


    QStringList keys = QStyleFactory::keys();
    keys.sort();
    foreach(const QString &key, keys) {
        styleCombo->addItem(key);
    }

    update_theme_combobox(themepath);


    // Hmm, there seems no way to get the name of the current
    // used style, using the classname minus Q and Style seems to do the trick.
    QString systemstyle = QString(QApplication::style()->metaObject()->className()).remove("Q").remove("Style");
    QString style = config().get_property("Themer", "style", systemstyle).toString();
        QString theme  = config().get_property("Themer", "currenttheme", "Traverso Light").toString();
    int coloradjust = config().get_property("Themer", "coloradjust", 100).toInt();
    bool usestylepallete = config().get_property("Themer", "usestylepallet", "").toBool();
    bool paintRectified = config().get_property("Themer", "paintaudiorectified", false).toBool();
    bool paintStereoAsMono = config().get_property("Themer", "paintstereoaudioasmono", false).toBool();
    bool paintWaveWithLines = config().get_property("Themer", "paintwavewithoutline", true).toBool();
    bool dbGrid = config().get_property("Themer", "drawdbgrid", false).toBool();
        Qt::Orientation orientation = (Qt::Orientation)config().get_property("Themer", "VUOrientation", Qt::Vertical).toInt();


    QString interfaceLanguage = config().get_property("Interface", "LanguageFile", "").toString();

    int index = styleCombo->findText(style);
    styleCombo->setCurrentIndex(index);
    index = themeSelecterCombo->findText(theme);
    themeSelecterCombo->setCurrentIndex(index);
    colorAdjustBox->setValue(coloradjust);
    useStylePalletCheckBox->setChecked(usestylepallete);
    themePathLineEdit->setText(themepath);
    rectifiedCheckBox->setChecked(paintRectified);
    mergedCheckBox->setChecked(paintStereoAsMono);
    dbGridCheckBox->setChecked(dbGrid);
    paintAudioWithOutlineCheckBox->setChecked(paintWaveWithLines);
        trackVUOrientationCheckBox->setChecked(orientation == Qt::Horizontal ? true : false);

    toolbarStyleCombo->clear();
    toolbarStyleCombo->addItem(tr("Icons only"));
    toolbarStyleCombo->addItem(tr("Text only"));
    toolbarStyleCombo->addItem(tr("Text beside Icons"));
    toolbarStyleCombo->addItem(tr("Text below Icons"));
    int tbstyle = config().get_property("Themer", "toolbuttonstyle", 0).toInt();
    toolbarStyleCombo->setCurrentIndex(tbstyle);

    // icon sizes of the toolbars
    QString iconsize = config().get_property("Themer", "iconsize", "22").toString();
    supportedIconSizes = config().get_property("Themer", "supportediconsizes", "16;22;32;48").toString();

    // if the list is empty, we should offer some default values. (The list can only be
    // empty if someone deleted the values, but not the whole entry, in the config file.)
    if (supportedIconSizes.isEmpty()) {
        supportedIconSizes = "16;22;32;48";
    }

    QStringList iconSizesList = supportedIconSizes.split(";", Qt::SkipEmptyParts);

    // check if the current icon size occurs in the list, if not, add it
    if (iconSizesList.lastIndexOf(iconsize) == -1) {
        iconSizesList << iconsize;
        iconSizesList.sort();
    }

    iconSizeCombo->clear();
    iconSizeCombo->addItems(iconSizesList);
    int iconsizeindex = iconSizeCombo->findText(iconsize);
    iconSizeCombo->setCurrentIndex(iconsizeindex);

    // and the same again for the icons size of the transport console
    QString trspsize = config().get_property("Themer", "transportconsolesize", "22").toString();
    iconSizesList = supportedIconSizes.split(";", Qt::SkipEmptyParts);

    if (iconSizesList.lastIndexOf(iconsize) == -1) {
        iconSizesList << trspsize;
        iconSizesList.sort();
    }

    transportConsoleCombo->clear();
    transportConsoleCombo->addItems(iconSizesList);
    int trspsizeindex = iconSizeCombo->findText(trspsize);
    transportConsoleCombo->setCurrentIndex(trspsizeindex);


    int langIndex = languageComboBox->findData(interfaceLanguage);
    if (langIndex >= 0) {
        languageComboBox->setCurrentIndex(langIndex);
        }
}

void AppearenceConfigPage::reset_default_config()
{
    styleCombo->clear();

    config().set_property("Themer", "themepath", QString(QDir::homePath()).append("/.traverso/themes"));
        config().set_property("Themer", "currenttheme", "Traverso Light");
    config().set_property("Themer", "coloradjust", 100);
    QString systemstyle = QString(QApplication::style()->metaObject()->className()).remove("Q").remove("Style");
    config().set_property("Themer", "style", systemstyle);
    config().set_property("Themer", "usestylepallet", false);
    config().set_property("Themer", "paintaudiorectified", false);
    config().set_property("Themer", "paintstereoaudioasmono", false);
    config().set_property("Themer", "drawdbgrid", false);
    config().set_property("Themer", "paintwavewithoutline", true);
    config().set_property("Themer", "supportediconsizes", "16;22;32;48");
    config().set_property("Themer", "iconsize", "22");
    config().set_property("Themer", "toolbuttonstyle", 0);
    config().set_property("Interface", "LanguageFile", "");
        config().set_property("Themer", "VUOrientation", Qt::Vertical);

    load_config();
}


AppearenceConfigPage::AppearenceConfigPage(QWidget * parent)
    : ConfigPage(parent)
{
    setupUi(this);

    themeSelecterCombo->setInsertPolicy(QComboBox::InsertAlphabetically);
        m_colorModifierDialog = 0;

    languageComboBox->addItem(tr("Default Language"), "");
    foreach(const QString &lang, find_qm_files()) {
        languageComboBox->addItem(language_name_from_qm_file(lang), lang);
    }

    load_config();
        create_connections();
}

void AppearenceConfigPage::create_connections()
{
        connect(styleCombo, &QComboBox::textActivated, this, &AppearenceConfigPage::style_index_changed);
        connect(themeSelecterCombo, &QComboBox::textActivated, this, &AppearenceConfigPage::theme_index_changed);
    connect(useStylePalletCheckBox, &QCheckBox::toggled, this, &AppearenceConfigPage::use_selected_styles_pallet_checkbox_toggled);
    connect(pathSelectButton, &QPushButton::clicked, this, &AppearenceConfigPage::dirselect_button_clicked);
    connect(colorAdjustBox, &QSpinBox::valueChanged, this, &AppearenceConfigPage::color_adjustbox_changed);
    connect(rectifiedCheckBox, &QCheckBox::toggled, this, &AppearenceConfigPage::theme_option_changed);
    connect(mergedCheckBox, &QCheckBox::toggled, this, &AppearenceConfigPage::theme_option_changed);
    connect(dbGridCheckBox, &QCheckBox::toggled, this, &AppearenceConfigPage::theme_option_changed);
    connect(paintAudioWithOutlineCheckBox, &QCheckBox::toggled, this, &AppearenceConfigPage::theme_option_changed);
        connect(trackVUOrientationCheckBox, &QCheckBox::toggled, this, &AppearenceConfigPage::theme_option_changed);
        connect(editThemePushButton, &QPushButton::clicked, this, &AppearenceConfigPage::edit_theme_button_clicked);
}

void AppearenceConfigPage::style_index_changed(const QString& text)
{
    QApplication::setStyle(text);
    QIcon icon = QApplication::style()->standardIcon(QStyle::SP_DirClosedIcon);
    pathSelectButton->setIcon(icon);
    use_selected_styles_pallet_checkbox_toggled(useStylePalletCheckBox->isChecked());
}

void AppearenceConfigPage::theme_index_changed(const QString & theme)
{
    int index = themeSelecterCombo->findText(theme);
    QString data = themeSelecterCombo->itemData(index).toString();
    QString path = config().get_property("Themer", "themepath", "").toString();

    if (data == "builtintheme") {
        themer()->use_builtin_theme(theme);
    } else {
                themer()->set_path_and_theme(path, theme + ".xml");
    }
}

void AppearenceConfigPage::use_selected_styles_pallet_checkbox_toggled(bool checked)
{
    // if (checked) {
    //     QApplication::setPalette(QApplication::style()->standardPalette());
    // } else {
    //     QApplication::setPalette(themer()->system_palette());
    // }
}

void AppearenceConfigPage::color_adjustbox_changed(int value)
{
    themer()->set_color_adjust_value(value);
}

void AppearenceConfigPage::dirselect_button_clicked()
{
    QString path = themePathLineEdit->text();
    if (path.isEmpty()) {
        path = QDir::homePath();
    }
    QString dirName = QFileDialog::getExistingDirectory(this,
            tr("Select default project dir"), path);

    if (!dirName.isEmpty()) {
        themePathLineEdit->setText(dirName);
        update_theme_combobox(dirName);
    }
}

void AppearenceConfigPage::update_theme_combobox(const QString& path)
{
    themeSelecterCombo->clear();

        foreach(QString theme, themer()->get_builtin_themes()) {
                themeSelecterCombo->addItem(theme, "builtintheme");
    }

    QDir themedir(path);
        foreach (QString themeName, themedir.entryList(QDir::Files)) {
                themeName = themeName.remove(".xml");
                QString filename = path + "/" + themeName;
                if (QFile::exists(filename + ".xml") ) {
                        themeSelecterCombo->addItem(themeName);
        }
    }

}

void AppearenceConfigPage::theme_option_changed()
{
    config().set_property("Themer", "paintaudiorectified", rectifiedCheckBox->isChecked());
    config().set_property("Themer", "paintstereoaudioasmono", mergedCheckBox->isChecked());
    config().set_property("Themer", "drawdbgrid", dbGridCheckBox->isChecked());
    config().set_property("Themer", "paintwavewithoutline", paintAudioWithOutlineCheckBox->isChecked());
        config().set_property("Themer", "VUOrientation", trackVUOrientationCheckBox->isChecked() ? Qt::Horizontal : Qt::Vertical);
        themer()->load();
}

void AppearenceConfigPage::edit_theme_button_clicked()
{
        if (!m_colorModifierDialog) {
                m_colorModifierDialog = new ThemeModifierDialog(this);
        }
        m_colorModifierDialog->show();
}



/****************************************/
/*            Behavior                  */
/****************************************/

BehaviorConfigPage::BehaviorConfigPage(QWidget * parent)
    : ConfigPage(parent)
{
    setupUi(this);

    connect(&config(), &TConfig::configChanged, this, &BehaviorConfigPage::update_follow);

    load_config();
}


void BehaviorConfigPage::save_config()
{
    config().set_property("Sheet", "trackCreationCount", numberOfTrackSpinBox->value());
    config().set_property("PlayHead", "Follow", keepCursorVisibleCheckBox->isChecked());
    config().set_property("PlayHead", "Scrollmode", scrollModeComboBox->currentIndex());
    config().set_property("AudioClip", "SyncDuringDrag", resyncAudioCheckBox->isChecked());
    config().set_property("AudioClip", "LockByDefault", lockClipsCheckBox->isChecked());
    config().set_property("ShortCuts", "ShowCursorHelp", showShortcutUsageMessagesCheckBox->isChecked());

    QString onopenaction;
    if (welcomeRadioButton->isChecked()) {
        config().set_property("Project", "welcome", "welcome");
    } else if (openLastProjectRadioButton->isChecked()) {
        config().set_property("Project", "welcome", "restore");
    } else {
        config().set_property("Project", "welcome", "welcome");
    }
    
    QString oncloseaction;
    if (saveRadioButton->isChecked()) {
        config().set_property("Project", "onclose", "save");
    } else if (askRadioButton->isChecked()) {
        config().set_property("Project", "onclose", "ask");
    } else {
        config().set_property("Project", "onclose", "dontsave");
    }
}

void BehaviorConfigPage::load_config()
{
    QString onopeneaction = config().get_property("Project", "welcome", "welcome").toString();
    QString oncloseaction = config().get_property("Project", "onclose", "save").toString();
    int defaultNumTracks = config().get_property("Sheet", "trackCreationCount", 1).toInt();
    int scrollMode = config().get_property("PlayHead", "Scrollmode", 2).toInt();
    bool resyncAudio = config().get_property("AudioClip", "SyncDuringDrag", false).toBool();
    bool lockClips = config().get_property("AudioClip", "LockByDefault", false).toBool();
    bool showShortcutHelpMessages = config().get_property("ShortCuts", "ShowCursorHelp", true).toBool();

    numberOfTrackSpinBox->setValue(defaultNumTracks);
    scrollModeComboBox->setCurrentIndex(scrollMode);
    resyncAudioCheckBox->setChecked(resyncAudio);
    lockClipsCheckBox->setChecked(lockClips);
    showShortcutUsageMessagesCheckBox->setChecked(showShortcutHelpMessages);

    if (onopeneaction == "welcome") {
        welcomeRadioButton->setChecked(true);
    } else if (onopeneaction == "restore") {
        openLastProjectRadioButton->setChecked(true);
    } else {
        welcomeRadioButton->setChecked(true);
    }

    if (oncloseaction == "save") {
        saveRadioButton->setChecked(true);
    } else if (oncloseaction == "ask") {
        askRadioButton->setChecked(true);
    } else {
        neverRadioButton->setChecked(true);
    }

    update_follow();
}


void BehaviorConfigPage::update_follow()
{
    bool keepCursorVisible = config().get_property("PlayHead", "Follow", true).toBool();
    keepCursorVisibleCheckBox->setChecked(keepCursorVisible);
    scrollModeComboBox->setEnabled(keepCursorVisible);
}

void BehaviorConfigPage::reset_default_config()
{
    config().set_property("Project", "welcome", "welcome");
    config().set_property("Project", "onclose", "save");
    config().set_property("Sheet", "trackCreationCount", 1);
    config().set_property("PlayHead", "Follow", 0);
    config().set_property("PlayHead", "Scrollmode", 2);
    config().set_property("AudioClip", "SyncDuringDrag", false);
    config().set_property("AudioClip", "LockByDefault", false);
    config().set_property("ShortCuts", "ShowCursorHelp", true);

    load_config();
}




/****************************************/
/*            Keyboard                  */
/****************************************/


KeyboardConfigPage::KeyboardConfigPage(QWidget * parent)
    : ConfigPage(parent)
{
    setupUi(this);
    load_config();
    connect(shortcutsKeymapComboBox, &QComboBox::activated, this, &KeyboardConfigPage::keymap_index_changed);
}

void KeyboardConfigPage::load_config()
{
    int jogByPassDistance = config().get_property("InputEventDispatcher", "jobbypassdistance", 70).toInt();
    int mouseClickTakesOverKeyboardNavigation = config().get_property("InputEventDispatcher", "mouseclicktakesoverkeyboardnavigation", false).toBool();
    bool enterFinishesHold = config().get_property("InputEventDispatcher", "EnterFinishesHold", false).toBool();
    bool useSimplifiedShortcuts = config().get_property("InputEventDispatcher", "UseSimplifiedShortcuts", true).toBool();

    mouseTreshHoldSpinBox->setValue(jogByPassDistance);

    if (mouseClickTakesOverKeyboardNavigation) {
        leftMouseClickRadioButton->setChecked(true);
    } else {
        mouseMoveRadioButton->setChecked(true);
    }

    if (enterFinishesHold) {
        enterPressedRadioButton->setChecked(true);
    } else {
        keyReleasedRadioButton->setChecked(true);
    }

    shortcutsKeymapComboBox->setCurrentIndex(useSimplifiedShortcuts ? 0 : 1);
}

void KeyboardConfigPage::save_config()
{
    config().set_property("InputEventDispatcher", "jobbypassdistance", mouseTreshHoldSpinBox->value());
    config().set_property("InputEventDispatcher", "mouseclicktakesoverkeyboardnavigation", leftMouseClickRadioButton->isChecked());
    config().set_property("InputEventDispatcher", "EnterFinishesHold", enterPressedRadioButton->isChecked());
    config().set_property("InputEventDispatcher", "UseSimplifiedShortcuts", shortcutsKeymapComboBox->currentIndex() != 1);

    cpointer().set_jog_bypass_distance(mouseTreshHoldSpinBox->value());
    cpointer().set_left_mouse_click_bypasses_jog(leftMouseClickRadioButton->isChecked());
}

void KeyboardConfigPage::reset_default_config()
{
    config().set_property("InputEventDispatcher", "jobbypassdistance", 70);
    config().set_property("InputEventDispatcher", "mouseclicktakesoverkeyboardnavigation", false);
    config().set_property("InputEventDispatcher", "EnterFinishesHold", false);
    config().set_property("InputEventDispatcher", "UseSimplifiedShortcuts", true);
    load_config();
}

void KeyboardConfigPage::on_exportButton_clicked()
{
    tShortCutManager().export_keymap();
    QMessageBox::information( TMainWindow::instance(), tr("KeyMap Export"),
             tr("The exported keymap can be found here:\n\n %1").arg(QDir::homePath() + "/traversokeymap.html"),
             QMessageBox::Ok);
}

void KeyboardConfigPage::on_editKeymapButton_clicked()
{
    TMainWindow::instance()->show_shortcuts_edit_dialog();
}

void KeyboardConfigPage::keymap_index_changed(int index)
{
    shortcutsKeymapComboBox->setCurrentIndex(index);
    if (index != 1) {
        config().set_property("InputEventDispatcher", "UseSimplifiedShortcuts", true);
    } else {
        config().set_property("InputEventDispatcher", "UseSimplifiedShortcuts", false);
    }
    tShortCutManager().unload_shortcuts();
    tShortCutManager().load_shortcuts();
}


PerformanceConfigPage::PerformanceConfigPage(QWidget* parent)
    : ConfigPage(parent)
{
    delete layout();
    setupUi(this);

    load_config();

    QIcon icon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxInformation);
    reloadWarningLabel->setPixmap(icon.pixmap(22, 22));
}


void PerformanceConfigPage::load_config()
{
    double buffertime = config().get_property("Hardware", "readbuffersize", 1.0).toDouble();
    bufferTimeSpinBox->setValue(buffertime);
}

void PerformanceConfigPage::save_config()
{
    double buffertime = bufferTimeSpinBox->value();
    config().set_property("Hardware", "readbuffersize", buffertime);
}

void PerformanceConfigPage::reset_default_config()
{
    config().set_property("Hardware", "readbuffersize", 1.0);
    load_config();
}


/****************************************/
/*            Recording                 */
/****************************************/

RecordingConfigPage::RecordingConfigPage(QWidget * parent)
    : ConfigPage(parent)
{
    setupUi(this);

    encodingComboBox->addItem("WAV", "wav");
    encodingComboBox->addItem("WavPack", "wavpack");
    encodingComboBox->addItem("WAV64", "w64");
    wavpackCompressionComboBox->addItem("Very high", "very_high");
    wavpackCompressionComboBox->addItem("High", "high");
    wavpackCompressionComboBox->addItem("Fast", "fast");

    // The qualities of the resampler in use
    const TResamplerChoice::Backend resampler = configured_resampler_backend();
    for (int quality : TAudioResampler::qualities(resampler)) {
        ontheflyResampleComboBox->addItem(TAudioResampler::quality_name(resampler, quality), quality);
    }

    connect(encodingComboBox, &QComboBox::activated, this, &RecordingConfigPage::encoding_index_changed);
    connect(useResamplingCheckBox, &QCheckBox::checkStateChanged, this, &RecordingConfigPage::use_onthefly_resampling_checkbox_changed);

    // Takes line up with what was heard while recording
    // (doc/design/recording-latency.md)
    auto* latencyGroup = new QGroupBox(tr("Latency Compensation"), this);
    auto* latencyLayout = new QGridLayout(latencyGroup);
    m_latencyCompensationCheckBox = new QCheckBox(tr("Compensate recordings for latency"), latencyGroup);
    m_latencyCompensationCheckBox->setToolTip(tr("Places a recorded take where it was played: earlier by the time from "
                                                 "hearing the playback to the played sound coming in (capture plus playback latency)"));
    m_extraLatencySpinBox = new QDoubleSpinBox(latencyGroup);
    m_extraLatencySpinBox->setRange(-50.0, 50.0);
    m_extraLatencySpinBox->setDecimals(2);
    m_extraLatencySpinBox->setSingleStep(0.1);
    m_extraLatencySpinBox->setSuffix(tr(" ms"));
    m_extraLatencySpinBox->setToolTip(tr("Added to what the driver reports, for the converters and external gear. "
                                         "Find it with a loopback: record a click played out, and adjust until the "
                                         "recorded click lines up with the original"));
    m_driverLatencyLabel = new QLabel(latencyGroup);
    latencyLayout->addWidget(m_latencyCompensationCheckBox, 0, 0, 1, 2);
    latencyLayout->addWidget(new QLabel(tr("Extra latency"), latencyGroup), 1, 0);
    latencyLayout->addWidget(m_extraLatencySpinBox, 1, 1);
    latencyLayout->addWidget(m_driverLatencyLabel, 2, 0, 1, 2);
    connect(m_latencyCompensationCheckBox, &QCheckBox::toggled, m_extraLatencySpinBox, &QWidget::setEnabled);
    verticalLayout->insertWidget(verticalLayout->count() - 1, latencyGroup);   // above the spacer
    // Another driver (the Audio page) or a measured change
    connect(&audiodevice(), &TAudioDevice::latencyChanged, this, &RecordingConfigPage::show_driver_latency);
    connect(&audiodevice(), &TAudioDevice::started, this, &RecordingConfigPage::show_driver_latency);
    // The files of takes that were undone and can not come back
    auto* discardGroup = new QGroupBox(tr("Discarded Recordings"), this);
    auto* discardLayout = new QHBoxLayout(discardGroup);
    discardLayout->addWidget(new QLabel(tr("Audio files of undone takes"), discardGroup));
    m_discardedTakesComboBox = new QComboBox(discardGroup);
    m_discardedTakesComboBox->addItem(tr("Ask"), "ask");
    m_discardedTakesComboBox->addItem(tr("Delete"), "delete");
    m_discardedTakesComboBox->addItem(tr("Keep"), "keep");
    m_discardedTakesComboBox->setToolTip(tr("What happens to the audio files of recorded takes that were undone and can no longer be "
                                            "redone: after a new edit, Edit > Discard Last Recording, or when the project closes"));
    discardLayout->addStretch();
    discardLayout->addWidget(m_discardedTakesComboBox);
    verticalLayout->insertWidget(verticalLayout->count() - 1, discardGroup);   // above the spacer

    load_config();
}

void RecordingConfigPage::show_driver_latency()
{
    const uint rate = audiodevice().get_sample_rate();
    if (rate == 0) {
        m_driverLatencyLabel->setText(tr("The driver reports no latency (no device running)"));
        return;
    }
    auto ms = [rate](nframes_t frames) { return QString::number(double(frames) * 1000.0 / rate, 'f', 2); };
    const nframes_t capture = audiodevice().get_capture_latency_frames();
    const nframes_t playback = audiodevice().get_playback_latency_frames();
    m_driverLatencyLabel->setText(tr("The driver reports: capture %1 ms, playback %2 ms, together %3 ms")
                                  .arg(ms(capture), ms(playback), ms(capture + playback)));
}

void RecordingConfigPage::load_config()
{
    show_driver_latency();

    bool useResampling = config().get_property("Conversion", "DynamicResampling", true).toBool();
    if (useResampling) {
        use_onthefly_resampling_checkbox_changed(Qt::Checked);
    } else {
        use_onthefly_resampling_checkbox_changed(Qt::Unchecked);
    }

    QString recordFormat = config().get_property("Recording", "FileFormat", "wav").toString();
    if (recordFormat == "wavpack") {
        encoding_index_changed(1);
    } else if (recordFormat == "w64") {
        encoding_index_changed(2);
    } else {
        encoding_index_changed(0);
    }

    m_latencyCompensationCheckBox->setChecked(config().get_property("Recording", "LatencyCompensation", true).toBool());
    m_extraLatencySpinBox->setValue(config().get_property("Recording", "ExtraLatency", 0.0).toDouble());
    m_extraLatencySpinBox->setEnabled(m_latencyCompensationCheckBox->isChecked());
    const int discarded = m_discardedTakesComboBox->findData(config().get_property("Recording", "DiscardedTakes", "ask").toString());
    m_discardedTakesComboBox->setCurrentIndex(discarded >= 0 ? discarded : 0);

    QString wavpackcompression = config().get_property("Recording", "WavpackCompressionType", "fast").toString();
    if (wavpackcompression == "very_high") {
        wavpackCompressionComboBox->setCurrentIndex(0);
    } else if (wavpackcompression == "high") {
        wavpackCompressionComboBox->setCurrentIndex(1);
    } else {
        wavpackCompressionComboBox->setCurrentIndex(2);
    }

    int index = ontheflyResampleComboBox->findData(configured_resampler().choice.quality);
    ontheflyResampleComboBox->setCurrentIndex(index);

    index = config().get_property("Conversion", "ExportResamplingConverterType", 1).toInt();
    exportDefaultResampleQualityComboBox->setCurrentIndex(index);
}

void RecordingConfigPage::save_config()
{
    config().set_property("Conversion", "DynamicResampling", useResamplingCheckBox->isChecked());
    config().set_property("Conversion", "RTResamplingQuality", ontheflyResampleComboBox->currentData().toInt());
    config().set_property("Conversion", "ExportResamplingConverterType", exportDefaultResampleQualityComboBox->currentIndex());
    config().set_property("Recording", "FileFormat", encodingComboBox->itemData(encodingComboBox->currentIndex()).toString());
    config().set_property("Recording", "LatencyCompensation", m_latencyCompensationCheckBox->isChecked());
    config().set_property("Recording", "ExtraLatency", m_extraLatencySpinBox->value());
    config().set_property("Recording", "DiscardedTakes", m_discardedTakesComboBox->currentData().toString());
    config().set_property("Recording", "WavpackCompressionType", wavpackCompressionComboBox->itemData(wavpackCompressionComboBox->currentIndex()).toString());
    QString skipwvx = wavpackUseAlmostLosslessCheckBox->isChecked() ? "true" : "false";
    config().set_property("Recording", "WavpackSkipWVX", skipwvx);
}

void RecordingConfigPage::reset_default_config()
{
    config().set_property("Conversion", "DynamicResampling", true);
    config().set_property("Conversion", "RTResamplingQuality",
                          TAudioResampler::default_realtime_quality(configured_resampler_backend()));
    config().set_property("Conversion", "ExportResamplingConverterType", 1);
    config().set_property("Recording", "FileFormat", "wav");
    config().set_property("Recording", "LatencyCompensation", true);
    config().set_property("Recording", "ExtraLatency", 0.0);
    config().set_property("Recording", "DiscardedTakes", "ask");
    config().set_property("Recording", "WavpackCompressionType", "fast");
    config().set_property("Recording", "WavpackSkipWVX", "false");

    load_config();
}


void RecordingConfigPage::encoding_index_changed(int index)
{
    encodingComboBox->setCurrentIndex(index);
    if (index != 1) {
        wacpackGroupBox->hide();
    } else {
        wacpackGroupBox->show();
    }
}

void RecordingConfigPage::use_onthefly_resampling_checkbox_changed(int state)
{
    if (state == Qt::Checked) {
        useResamplingCheckBox->setChecked(true);
        ontheflyResampleComboBox->setEnabled(true);
    } else {
        useResamplingCheckBox->setChecked(false);
        ontheflyResampleComboBox->setEnabled(false);
    }
}

