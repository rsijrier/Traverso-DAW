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

#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QStyle>
#include <QStyleFactory>

#include "Pages.h"
#include "ResampleAudioReader.h"
#include "dialogs/ThemeModifierDialog.h"
#include <AudioDevice.h>
#if defined (ALSA_SUPPORT)
#include <AlsaDriver.h>
#endif

#if defined (PORTAUDIO_SUPPORT)
#include "PADriver.h"
#endif

#include "TConfig.h"
#include <Utils.h>
#include <Themer.h>
#include "TInputEventDispatcher.h"
#include "ContextPointer.h"
#include "TMainWindow.h"
#include "TShortCutManager.h"
#include <QDomDocument>


/****************************************/
/*            AudioDriver               */
/****************************************/


AudioDriverConfigPage::AudioDriverConfigPage(QWidget *parent)
    : ConfigPage(parent)
{
    setupUi(this);
        driverInformationTextEdit->setTextInteractionFlags(Qt::NoTextInteraction);
        driverInformationTextEdit->hide();

    periodBufferSizesList << 16 << 32 << 64 << 128 << 256 << 512 << 1024 << 2048 << 4096;

    m_mainLayout = qobject_cast<QVBoxLayout*>(layout());

    QStringList drivers = audiodevice().get_available_drivers();
    foreach(const QString &name, drivers) {
        driverCombo->addItem(name);
    }


    m_portaudiodrivers = new PaDriverPage(this);
    m_mainLayout->addWidget(m_portaudiodrivers);

    m_alsadevices = new AlsaDevicesPage(this);
    m_alsadevices->layout()->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->addWidget(m_alsadevices);

        connect(driverCombo, SIGNAL(currentTextChanged(QString)), this, SLOT(driver_combobox_index_changed(QString)));
    connect(restartDriverButton, SIGNAL(clicked()), this, SLOT(restart_driver_button_clicked()));
        connect(rateComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(rate_combobox_index_changed(int)));
    connect(&audiodevice(), SIGNAL(driverSetupMessage(QString,int)), this, SLOT(driver_setup_message(QString,int)));
        connect(&audiodevice(), SIGNAL(message(QString,int)), this, SLOT(driver_setup_message(QString,int)));

#if defined (PORTAUDIO_SUPPORT)
        connect(m_portaudiodrivers->driverCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(portaudio_host_api_combobox_index_changed(int)));
#endif
    load_config();
}

void AudioDriverConfigPage::save_config()
{
    config().set_property("Hardware", "samplerate", rateComboBox->currentText());
    int bufferindex = latencyComboBox->currentIndex();
    int buffersize = 1024;
    if (bufferindex >= 0) {
        buffersize = periodBufferSizesList.at(bufferindex);
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
    if (libjack_is_present)
        config().set_property("Hardware", "drivertype", "Jack");
#else
    config().set_property("Hardware", "drivertype", "Null Driver");
#endif

#if defined (PORTAUDIO_SUPPORT)
#if defined (Q_OS_UNIX)
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
#if defined (Q_OS_UNIX)
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

    int buffersizeIndex = periodBufferSizesList.indexOf(buffersize);
    int samplerateIndex = rateComboBox->findText(QString::number(samplerate));

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

    // Iterate over the maximum number of devices that can be in a system
    // according to alsa, and add them to the devices list.
    QString name, longName;
    for (int i=0; i<6; ++i) {
        name = AlsaDriver::alsa_device_name(i);
        longName = AlsaDriver::alsa_device_longname(i);
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

#if defined (Q_OS_UNIX)
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
        m_driverSetupMessages.clear();
        driverInformationTextEdit->clear();

        TAudioDeviceSetup ads = audiodevice().get_device_setup();
    QString driver = driverCombo->currentText();
        ads.rate = rateComboBox->currentText().toInt();
        ads.bufferSize =  periodBufferSizesList.at(latencyComboBox->currentIndex());

        ads.playback = true;
        ads.capture = true;
    if(duplexComboBox->currentIndex() == 1) {
                ads.capture = false;
    }

    if(duplexComboBox->currentIndex() == 2) {
                ads.playback = false;
    }


        ads.cardDevice = "";
        ads.ditherShape = "None";


#if defined (ALSA_SUPPORT)
    int periods = m_alsadevices->periodsCombo->currentText().toInt();
        ads.ditherShape = m_alsadevices->ditherShapeComboBox->currentText();
    // The AlsaDriver retrieves it's periods number directly from config()
    // So there is no way to use the current selected one, other then
    // setting it now, and restoring it afterwards...
    int currentperiods = config().get_property("Hardware", "numberofperiods", 3).toInt();
    config().set_property("Hardware", "numberofperiods", periods);

    if (driver == "ALSA") {
        int index = m_alsadevices->devicesCombo->currentIndex();
                ads.cardDevice = m_alsadevices->devicesCombo->itemData(index).toString();
    }
#endif

#if defined (PORTAUDIO_SUPPORT)
    if (driver == "PortAudio") {
        int index = m_portaudiodrivers->driverCombo->currentIndex();
                ads.cardDevice = m_portaudiodrivers->driverCombo->itemData(index).toString();
                ads.cardDevice += "::" + m_portaudiodrivers->inputDevicesCombo->currentText();
                ads.cardDevice += "::" + m_portaudiodrivers->outputDevicesCombo->currentText();
        }
#endif

        ads.driverType = driver;
        audiodevice().set_parameters(ads);

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
}

#if defined (PORTAUDIO_SUPPORT)
void AudioDriverConfigPage::portaudio_host_api_combobox_index_changed(int index)
{
        if (m_portaudiodrivers->isHidden()) {
                return;
        }

        QStringList list = PADriver::devices_info(m_portaudiodrivers->driverCombo->itemData(index).toString());

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
    int rate = rateComboBox->currentText().toInt();
    int buffersize = audiodevice().get_buffer_size();

    for (int i=0; i<periodBufferSizesList.size(); ++i) {
        QString latency = QString::number( ((float)(periodBufferSizesList.at(i)) / rate) * 1000 * 2, 'f', 2);
        latencyComboBox->addItem(latency);
    }

    int index = periodBufferSizesList.indexOf(buffersize);
    latencyComboBox->setCurrentIndex(index);
}

void AudioDriverConfigPage::rate_combobox_index_changed(int )
{
    update_latency_combobox();
}

void AudioDriverConfigPage::driver_setup_message(QString message, int severity)
{
        driverInformationTextEdit->clear();
        if (driverInformationTextEdit->isHidden()) {
                driverInformationTextEdit->show();
        }

        if (severity == AudioDevice::DRIVER_SETUP_FAILURE || severity == AudioDevice::CRITICAL) {
                m_driverSetupMessages.prepend("<p class=\"failure\">" + message + "</p>");
        } else if (severity == AudioDevice::DRIVER_SETUP_WARNING || severity == AudioDevice::WARNING) {
                m_driverSetupMessages.prepend("<p class=\"warning\">" + message + "</p>");
        } else if (severity == AudioDevice::DRIVER_SETUP_SUCCESS || severity == AudioDevice::DRIVER_SETUP_INFO || severity == AudioDevice::INFO) {
                m_driverSetupMessages.prepend("<p class=\"success\">" + message + "</p>");
        } else {
                m_driverSetupMessages.prepend("<p>" + message + "</p>");
        }

        QString html = QString("<html><head><meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n"
              "<style type=\"text/css\">\n"
              "p {font-size: 12px; }\n"
              ".failure {color: red;}\n"
              ".warning {color: darkorange;}\n"
              ".success {color: green;}\n"
              "</style>\n"
              "</head>\n<body>\n");

        foreach(QString string, m_driverSetupMessages) {
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
        connect(styleCombo, SIGNAL(textActivated(QString)), this, SLOT(style_index_changed(QString)));
        connect(themeSelecterCombo, SIGNAL(textActivated(QString)), this, SLOT(theme_index_changed(QString)));
    connect(useStylePalletCheckBox, SIGNAL(toggled(bool)), this, SLOT(use_selected_styles_pallet_checkbox_toggled(bool)));
    connect(pathSelectButton, SIGNAL(clicked()), this, SLOT(dirselect_button_clicked()));
    connect(colorAdjustBox, SIGNAL(valueChanged(int)), this, SLOT(color_adjustbox_changed(int)));
    connect(rectifiedCheckBox, SIGNAL(toggled(bool)), this, SLOT(theme_option_changed()));
    connect(mergedCheckBox, SIGNAL(toggled(bool)), this, SLOT(theme_option_changed()));
    connect(dbGridCheckBox, SIGNAL(toggled(bool)), this, SLOT(theme_option_changed()));
    connect(paintAudioWithOutlineCheckBox, SIGNAL(toggled(bool)), this, SLOT(theme_option_changed()));
        connect(trackVUOrientationCheckBox, SIGNAL(toggled(bool)), this, SLOT(theme_option_changed()));
        connect(editThemePushButton, SIGNAL(clicked()), this, SLOT(edit_theme_button_clicked()));
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
    if (checked) {
        QApplication::setPalette(QApplication::style()->standardPalette());
    } else {
        QApplication::setPalette(themer()->system_palette());
    }
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

    connect(&config(), SIGNAL(configChanged()), this, SLOT(update_follow()));

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
}

void KeyboardConfigPage::load_config()
{
    int jogByPassDistance = config().get_property("InputEventDispatcher", "jobbypassdistance", 70).toInt();
    int mouseClickTakesOverKeyboardNavigation = config().get_property("InputEventDispatcher", "mouseclicktakesoverkeyboardnavigation", false).toBool();
    bool enterFinishesHold = config().get_property("InputEventDispatcher", "EnterFinishesHold", false).toBool();

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
}

void KeyboardConfigPage::save_config()
{
    config().set_property("InputEventDispatcher", "jobbypassdistance", mouseTreshHoldSpinBox->value());
    config().set_property("InputEventDispatcher", "mouseclicktakesoverkeyboardnavigation", leftMouseClickRadioButton->isChecked());
    config().set_property("InputEventDispatcher", "EnterFinishesHold", enterPressedRadioButton->isChecked());

        cpointer().set_jog_bypass_distance(mouseTreshHoldSpinBox->value());
        cpointer().set_left_mouse_click_bypasses_jog(leftMouseClickRadioButton->isChecked());
}

void KeyboardConfigPage::reset_default_config()
{
    config().set_property("InputEventDispatcher", "jobbypassdistance", 70);
    config().set_property("InputEventDispatcher", "mouseclicktakesoverkeyboardnavigation", false);
    config().set_property("InputEventDispatcher", "EnterFinishesHold", false);
    load_config();
}

void KeyboardConfigPage::on_exportButton_clicked()
{
    tShortCutManager().export_keymap();
    QMessageBox::information( TMainWindow::instance(), tr("KeyMap Export"),
             tr("The exported keymap can be found here:\n\n %1").arg(QDir::homePath() + "/traversokeymap.html"),
             QMessageBox::Ok);
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

        connect(encodingComboBox, SIGNAL(activated(int)), this, SLOT(encoding_index_changed(int)));
    connect(useResamplingCheckBox, SIGNAL(stateChanged(int)),
        this, SLOT(use_onthefly_resampling_checkbox_changed(int)));

    load_config();
}

void RecordingConfigPage::load_config()
{
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

    QString wavpackcompression = config().get_property("Recording", "WavpackCompressionType", "fast").toString();
    if (wavpackcompression == "very_high") {
        wavpackCompressionComboBox->setCurrentIndex(0);
    } else if (wavpackcompression == "high") {
        wavpackCompressionComboBox->setCurrentIndex(1);
    } else {
        wavpackCompressionComboBox->setCurrentIndex(2);
    }

    int index = config().get_property("Conversion", "RTResamplingConverterType", ResampleAudioReader::get_default_resample_quality()).toInt();
    ontheflyResampleComboBox->setCurrentIndex(index);

    index = config().get_property("Conversion", "ExportResamplingConverterType", 1).toInt();
    exportDefaultResampleQualityComboBox->setCurrentIndex(index);
}

void RecordingConfigPage::save_config()
{
    config().set_property("Conversion", "DynamicResampling", useResamplingCheckBox->isChecked());
    config().set_property("Conversion", "RTResamplingConverterType", ontheflyResampleComboBox->currentIndex());
    config().set_property("Conversion", "ExportResamplingConverterType", exportDefaultResampleQualityComboBox->currentIndex());
    config().set_property("Recording", "FileFormat", encodingComboBox->itemData(encodingComboBox->currentIndex()).toString());
    config().set_property("Recording", "WavpackCompressionType", wavpackCompressionComboBox->itemData(wavpackCompressionComboBox->currentIndex()).toString());
    QString skipwvx = wavpackUseAlmostLosslessCheckBox->isChecked() ? "true" : "false";
    config().set_property("Recording", "WavpackSkipWVX", skipwvx);
}

void RecordingConfigPage::reset_default_config()
{
    config().set_property("Conversion", "DynamicResampling", true);
    config().set_property("Conversion", "RTResamplingConverterType", ResampleAudioReader::get_default_resample_quality());
    config().set_property("Conversion", "ExportResamplingConverterType", 1);
    config().set_property("Recording", "FileFormat", "wav");
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

