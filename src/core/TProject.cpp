/*
Copyright (C) 2005-2024 Remon Sijrier

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
#include <QDir>
#include <QTextStream>
#include <QMessageBox>
#include <QString>

#include <unistd.h>

#include "AudioBus.h"
#include "AudioChannel.h"
#include "TAudioTrack.h"
#include "TAudioDeviceClient.h"
#include "TProject.h"
#include "TSheet.h"
#include "TProjectManager.h"
#include "TIOScheduler.h"
#include "TInformUser.h"
#include "TInputEventDispatcher.h"
#include "TResourcesManager.h"
#include "TPeakBuildService.h"
#include "TExportSpecification.h"
#include "TAudioDevice.h"
#include "TConfig.h"
#include "TContextPointer.h"
#include "Utils.h"
#include <TAddRemoveCommand.h>
#include "TRtTransaction.h"
#include "FileHelpers.h"
#include "TTimeLineRuler.h"
#include "TBusTrack.h"
#include "TSend.h"
#include "SpectralMeter.h"
#include "CorrelationMeter.h"
#include "Debugger.h"

#include "qapplication.h"
#include "qthread.h"

#define PROJECT_FILE_VERSION 	3
#define MASTER_OUT_SOFTWARE_BUS_ID 1


/**	\class TProject
	\brief TProject restores and saves the state of a Traverso TProject
	
	A TProject can have as much Sheet's as one likes. A TProject with one Sheet acts like a 'Session'
	where the Sheet can be turned into a CD with various Tracks using Marker 's
	When a TProject has multiple Sheet's, each Sheet will be a CD Track, this can be usefull if each
	Track on a CD is independend of the other CD Tracks.
 
 */


TProject::TProject(const QString& title)
    : TSession()
{
    PENTERCONS;
    m_name = title;
    m_activeSheet = nullptr;
    m_spectralMeter = nullptr;
    m_correlationMeter = nullptr;
    m_activeSession = nullptr;
    m_activeSessionId = m_activeSheetId = -1;
    engineer = "";
    m_keyboardArrowNavigationSpeed = 4;
    m_projectClosed = false;
    set_is_project_session(true);

    m_useResampling = config().get_property("Conversion", "DynamicResampling", true).toBool();
    m_rootDir = config().get_property("Project", "directory", "/directory/unknown/").toString() + "/" + m_name;
    m_sourcesDir = m_rootDir + "/audiosources";
    m_rate = audiodevice().get_sample_rate();
    m_bitDepth = audiodevice().get_bit_depth();

    m_resourcesManager = new TResourcesManager(this);
    m_peakBuilds = std::make_unique<TPeakBuildService>();
    create_history_stack();

    m_audiodeviceClient = new TAudioDeviceClient("sheet_" + QByteArray::number(get_id()));
    // Made here, on the GUI thread, if not yet: process() only wakes them
    m_readScheduler = pm().get_read_scheduler();
    m_writeScheduler = pm().get_write_scheduler();
    m_audiodeviceClient->set_process_callback(
        TProcessCallBack::from_method<TProject, &TProject::process>(this)
        );

    m_audiodeviceClient->set_transport_control_callback(
        TTransportControlCallBack::from_method<TProject, &TProject::transport_control>(this)
        );

    m_exportSpecification = nullptr;

    m_masterOutBusTrack = new MasterOutSubGroup(this, "");
    // FIXME: m_masterOut is a Track, but at this point in time, Track can't
    // get a reference to us via pm().get_project();
    connect(m_masterOutBusTrack, &TBusTrack::routingConfigurationChanged, this, &TProject::track_property_changed);


    AudioBus* bus = m_masterOutBusTrack->get_process_bus();
    for(uint i=0; i<bus->get_channel_count(); i++) {
        if (AudioChannel* chan = bus->get_channel(i)) {
            chan->set_buffer_size(audiodevice().get_buffer_size());
        }
    }

    cpointer().add_contextitem(this);

    connect(this, &TProject::exportFinished, this, &TProject::export_finished, Qt::QueuedConnection);
    connect(&audiodevice(), &TAudioDevice::driverParamsChanged, this, &TProject::audiodevice_params_changed, Qt::DirectConnection);
    connect(&audiodevice(), &TAudioDevice::audioDeviceClientRemoved, this, &TProject::audio_device_removed_client);
}


TProject::~TProject()
{
    PENTERDES;

    cpointer().remove_contextitem(this);

    // The peak jobs are not needed anymore
    m_peakBuilds->cancel_all();

    // Need to delete AudioClips/AudioSource first before deleting Sheets
    delete m_resourcesManager;

    for(TSheet* sheet : m_sheets.gui_items()) {
        delete sheet;
    }
    for (AudioBus* bus : m_hardwareAudioBuses) {
        delete bus;
    }
    for (AudioBus* bus : m_softwareAudioBuses) {
        delete bus;
    }

    delete m_masterOutBusTrack;
}


int TProject::create(int sheetcount, int numtracks)
{
    PENTER;
    PMESG("Creating new project %s  NumSheets=%d", QS_C(m_name), sheetcount);

    QDir dir;
    if (!dir.mkdir(m_rootDir)) {
        tInformUser().critical(tr("Cannot create dir %1").arg(m_rootDir));
        return -1;
    }

    if (create_audiosources_dir() < 0) {
        return -1;
    }

    if (pm().create_projectfilebackup_dir(m_rootDir) < 0) {
        return -1;
    }

    {
        // Joins the transaction TProjectManager::create_new_project() opens
        // around creating the project: a transaction opened while another one
        // is open hands its changes to the outermost one, which publishes all
        // of them as one event when it ends.
        TRtTransaction transaction;
        for (int i=0; i< sheetcount; i++) {
            TSheet* sheet = new TSheet(this, numtracks);
            m_sheets.add(sheet, transaction);
        }
    }

    if (!m_sheets.gui_items().isEmpty()) {
        set_current_session(m_sheets.gui_items().first()->get_id());
    }

    m_importDir = QDir::homePath();

    // Only the setup: the audio device starts when the project loads. Another
    // project may still be connected to it now
    read_audio_device_setup(QDomDocument());

    tInformUser().information(tr("Created new Project %1").arg(m_name));
    return 1;
}

int TProject::create_audiosources_dir()
{
    QDir dir;
    if (!dir.mkdir(m_sourcesDir)) {
        tInformUser().critical(tr("Cannot create dir %1").arg(m_sourcesDir));
        return -1;
    }

    return 1;

}

int TProject::load(const QString& projectfile)
{
    PENTER;
    QDomDocument doc("Project");

    QFile file;
    QString filename;

    if (projectfile.isEmpty()) {
        filename = m_rootDir + "/project.tpf";
        file.setFileName(filename);
    } else {
        filename = projectfile;
        file.setFileName(filename);
    }

    if (!file.open(QIODevice::ReadOnly)) {
        m_errorString = tr("Project %1: Cannot open project.tpf file! (Reason: %2)").arg(m_name).arg(file.errorString());
        tInformUser().critical(m_errorString);
        return PROJECT_FILE_COULD_NOT_BE_OPENED;
    }

    // Check if important directories still exist!
    QDir dir;
    // Peak files live in the user's peak cache (TPeakCache) now; an old
    // project's directory is the user's to remove
    if (dir.exists(m_rootDir + "/peakfiles")) {
        TLOG_I("project", "old peak files in {} are no longer used and can be removed", (m_rootDir + "/peakfiles").toStdString());
    }
    if (!dir.exists(m_rootDir + "/audiosources")) {
        create_audiosources_dir();
    }


    // Start setting and parsing the content of the xml file
    QString errorMsg;
    QDomDocument::ParseResult result = doc.setContent(file.readAll());
    if (!result) {
        m_errorString = tr("Project %1: Failed to parse project.tpf file! (Reason: %2)").arg(m_name).arg(result.errorMessage);
        tInformUser().critical(m_errorString);
        return SETTING_XML_CONTENT_FAILED;
    }

    emit projectLoadStarted();

    QDomElement docElem = doc.documentElement();
    QDomNode propertiesNode = docElem.firstChildElement("Properties");
    QDomElement e = propertiesNode.toElement();

    if (e.attribute("projectfileversion", "-1").toInt() != PROJECT_FILE_VERSION) {
        m_errorString = tr("Project File Version does not match, unable to load Project!");
        tInformUser().warning(m_errorString);
        return PROJECT_FILE_VERSION_MISMATCH;
    }

    m_name = e.attribute( "title", "" );
    engineer = e.attribute( "engineer", "" );
    m_description = e.attribute( "description", "No description set");
    m_discid = e.attribute( "discId", "" );
    m_upcEan = e.attribute( "upc_ean", "" );
    m_genre = e.attribute( "genre", "" ).toInt();
    m_performer = e.attribute( "performer", "" );
    m_arranger = e.attribute( "arranger", "" );
    m_songwriter = e.attribute( "songwriter", "" );
    m_message = e.attribute( "message", "" );
    m_rate = e.attribute( "rate", "" ).toUInt();
    m_bitDepth = e.attribute( "bitdepth", "" ).toUInt();
    set_id(e.attribute("id", "0").toLongLong());
    m_sheetsAreTrackFolder = e.attribute("sheeTSMPetrackfolder", "0").toInt();
    m_importDir = e.attribute("importdir", QDir::homePath());



    QDomNode channelsConfigNode = docElem.firstChildElement("AudioChannels");
    QDomNode channelNode = channelsConfigNode.firstChild();

    //        while (!channelNode.isNull()) {
    //                QDomElement e = channelNode.toElement();
    //                QString name = e.attribute("name", "");
    //                QString typeString = e.attribute("type", "");
    //                uint number = e.attribute("number", "0").toInt();
    //                qint64 id = e.attribute("id", "0").toLongLong();

    //                int type = -1;
    //                if (typeString == "input") {
    //                        type = ChannelIsInput;
    //                }
    //                if (typeString == "output") {
    //                        type = ChannelIsOutput;
    //                }

    //                AudioChannel* chan = new AudioChannel(name, number, type, id);

    //                m_softwareAudioChannels.insert(chan->get_id(), chan);

    //                channelNode = channelNode.nextSibling();
    //        }


    QDomNode busesConfigNode = docElem.firstChildElement("AudioBuses");
    QDomNode busNode = busesConfigNode.firstChild();

    while (!busNode.isNull()) {
        TAudioBusConfiguration conf;
        QDomElement e = busNode.toElement();
        conf.name = e.attribute("name", "");
        conf.channelNames = e.attribute("channels", "").split(";");
        conf.type = e.attribute("type", "");
        conf.bustype = e.attribute("bustype", "software");
        conf.id = e.attribute("id", "-1").toLongLong();
        QStringList channelIds = e.attribute("channelids", "").split(";", Qt::SkipEmptyParts);

        AudioBus* bus = new AudioBus(conf);

        switch(bus->get_bus_type()) {
        case AudioBus::BusIsSoftware:
        {
            AudioChannel* channel;
            for(const QString &idString : channelIds) {
                qint64 id = idString.toLongLong();
                channel = m_softwareAudioChannels.value(id);
                if (channel) {
                    bus->add_channel(channel);
                }
            }
            m_softwareAudioBuses.insert(bus->get_id(), bus);
            break;
        }
        case AudioBus::BusIsHardware:
        {
            for(const QString &channelName : conf.channelNames) {
                bus->add_channel(channelName);
            }
            m_hardwareAudioBuses.append(bus);
            break;
        }
        default:
            PERROR("Unknown Bus type");
            delete bus;
        }

        busNode = busNode.nextSibling();
    }


    read_audio_device_setup(doc);
    audiodevice().set_parameters(m_audioDeviceSetup);

    QDomNode masterOutNode = docElem.firstChildElement("MasterOut");
    m_masterOutBusTrack->set_state(masterOutNode.firstChildElement());
    // Force the proper name for our Master Bus
    m_masterOutBusTrack->set_name(tr("Master"));

    // If master out doesn't have post sends, the user won't hear anything!
    // add the first logical 'hardware' output channels as post send if the
    // driver != Jack, the Jack driver case is handled seperately.
    if (!m_masterOutBusTrack->get_post_sends().size()) {
        if (audiodevice().get_driver_type() != "Jack") {
            AudioBus* bus = get_playback_bus("Playback 1-2");
            if (bus) {
                m_masterOutBusTrack->add_post_send(bus);
            }
        }
    }

    // Lets see if there is already a Project Master out jack bus:
    // if not, create one, the user expects at least that Master shows up in the patchbay!
    if (audiodevice().get_driver_type() == "Jack") {
        AudioBus* bus = m_softwareAudioBuses.value(MASTER_OUT_SOFTWARE_BUS_ID);
        if (!bus) {
            TAudioBusConfiguration conf;
            conf.name = "jackmaster";
            conf.channelNames << "jackmaster_0" << "jackmaster_1";
            conf.type = "output";
            conf.bustype = "software";
            conf.id = MASTER_OUT_SOFTWARE_BUS_ID;
            bus = create_software_audio_bus(conf);

            m_masterOutBusTrack->add_post_send(MASTER_OUT_SOFTWARE_BUS_ID);

        }
    }

    QDomNode busTracksNode = docElem.firstChildElement("BusTracks");
    QDomNode busTrackNode = busTracksNode.firstChild();

    {
        TRtTransaction transaction;

        while(!busTrackNode.isNull()) {
            TBusTrack* busTrack = new TBusTrack(this, busTrackNode);
            busTrack->set_state(busTrackNode);
            private_add_track(busTrack, transaction);

            busTrackNode = busTrackNode.nextSibling();
        }
    }

    // Load all the AudioSources for this project
    QDomNode asmNode = docElem.firstChildElement("ResourcesManager");
    m_resourcesManager->set_state(asmNode);


    QDomNode sheetsNode = docElem.firstChildElement("Sheets");
    QDomNode sheetNode = sheetsNode.firstChild();

    // Load all the Sheets
    while(!sheetNode.isNull())
    {
        TSheet* sheet = new TSheet(this, sheetNode);
        {
            // Joins the transaction TProjectManager::load_project() opens around
            // the load: a transaction opened while another one is open hands
            // its changes to the outermost one, which publishes all of them as
            // one event when it ends.
            TRtTransaction transaction;
            m_sheets.add(sheet, transaction);
        }

        sheet->set_state(sheetNode);

        sheetNode = sheetNode.nextSibling();
    }

    QDomNode workSheetsNode = docElem.firstChildElement("WorkSheets");
    QDomNode workSheetNode = workSheetsNode.firstChild();

    while(!workSheetNode.isNull()) {
        TSession* childSession = new TSession(this);
        childSession->set_state(workSheetNode);
        add_child_session(childSession);

        workSheetNode = workSheetNode.nextSibling();
    }


    qint64 activeSheetId = e.attribute("currentsheetid", "0" ).toLongLong();
    qint64 activeSessionId = e.attribute("activesessionid", "0" ).toLongLong();

    if ( activeSheetId == 0) {
        if (!m_sheets.gui_items().isEmpty()) {
            activeSheetId = m_sheets.gui_items().first()->get_id();
        }
    }

    if (activeSheetId == activeSessionId) {
        set_current_session(activeSheetId);
    } else {
        set_current_session(activeSheetId);
        set_current_session(activeSessionId);
    }

    tInformUser().information( tr("Project %1 loaded").arg(m_name) );

    emit projectLoadFinished();

    return 1;
}

int TProject::save_from_template_to_project_file(const QString& templateFile, const QString& projectName)
{
    QFile file(templateFile);
    QString saveFileName = m_rootDir + "/project.tpf";

    QDomDocument doc("Project");

    if (!file.open(QIODevice::ReadOnly)) {
        m_errorString = tr("Project %1: Cannot open project.tpf file! (Reason: %2)").arg(m_name, file.errorString());
        tInformUser().critical(m_errorString);
        return PROJECT_FILE_COULD_NOT_BE_OPENED;
    }

    // Start setting and parsing the content of the xml file
    QDomDocument::ParseResult result = doc.setContent(file.readAll());
    if (!result) {
        m_errorString = tr("Project %1: Failed to parse project.tpf file! (Reason: %2)").arg(m_name, result.errorMessage);
        tInformUser().critical(m_errorString);
        return SETTING_XML_CONTENT_FAILED;
    }

    QDomElement docElem = doc.documentElement();
    QDomNode propertiesNode = docElem.firstChildElement("Properties");
    QDomElement e = propertiesNode.toElement();

    if (e.attribute("projectfileversion", "-1").toInt() != PROJECT_FILE_VERSION) {
        m_errorString = tr("Project File Version does not match, unable to load Project!");
        tInformUser().warning(m_errorString);
        return PROJECT_FILE_VERSION_MISMATCH;
    }

    e.setAttribute("title", projectName);

    QString errorstring;
    if (!TFileHelper::save_document(doc, saveFileName, &errorstring)) {
        tInformUser().critical( tr("Couldn't write the Project properties file! (File %1. Reason: %2)").arg(saveFileName).arg(errorstring) );
        return -1;
    }

    return 1;
}


int TProject::save(bool autosave)
{
    PENTER;
    QDomDocument doc("Project");
    QString fileName = m_rootDir + "/project.tpf";

    get_state(doc);
    QString errorstring;
    if (!TFileHelper::save_document(doc, fileName, &errorstring)) {
        tInformUser().critical( tr("Couldn't write the Project properties file! (File %1. Reason: %2)").arg(fileName).arg(errorstring) );
        return -1;
    }

    // The project file no longer names the discarded recording takes: now
    // their files can go
    m_resourcesManager->delete_discarded_files();

    if (!autosave) {
        tInformUser().information( tr("Project %1 saved ").arg(m_name) );
    }

    pm().start_incremental_backup(this);

    return 1;
}


QDomNode TProject::get_state(QDomDocument doc, bool istemplate)
{
    PENTER;

    QDomElement projectNode = doc.createElement("Project");
    QDomElement properties = doc.createElement("Properties");

    properties.setAttribute("title", m_name);
    properties.setAttribute("engineer", engineer);
    properties.setAttribute("description", m_description);
    properties.setAttribute("discId", m_discid );
    properties.setAttribute("upc_ean", m_upcEan);
    properties.setAttribute("genre", QString::number(m_genre));
    properties.setAttribute("performer", m_performer);
    properties.setAttribute("arranger", m_arranger);
    properties.setAttribute("songwriter", m_songwriter);
    properties.setAttribute("message", m_message);
    properties.setAttribute("currentsheetid", m_activeSheetId);
    properties.setAttribute("activesessionid", m_activeSessionId);
    properties.setAttribute("rate", m_rate);
    properties.setAttribute("bitdepth", m_bitDepth);
    properties.setAttribute("projectfileversion", PROJECT_FILE_VERSION);
    properties.setAttribute("sheeTSMPetrackfolder", m_sheetsAreTrackFolder);
    if (! istemplate) {
        properties.setAttribute("id", get_id());
    } else {
        properties.setAttribute("title", "Template Project File!!");
    }
    properties.setAttribute("importdir", m_importDir);

    projectNode.appendChild(properties);


    QDomElement audioDriverConfigs = doc.createElement("AudioDriverConfigurations");
    QDomElement audioDriverConfig = doc.createElement("AudioDriverConfiguration");

    audioDriverConfig.setAttribute("device", m_audioDeviceSetup.get_card_device());
    audioDriverConfig.setAttribute("driver", m_audioDeviceSetup.get_driver_type());
    audioDriverConfig.setAttribute("samplerate", m_audioDeviceSetup.get_sample_rate());
    audioDriverConfig.setAttribute("buffersize", m_audioDeviceSetup.get_buffer_size());

    audioDriverConfigs.appendChild(audioDriverConfig);

    projectNode.appendChild(audioDriverConfigs);

    QDomElement channelsElement = doc.createElement("AudioChannels");

    foreach(AudioChannel* channel, m_softwareAudioChannels) {
        QDomElement chanElement = doc.createElement("Channel");
        chanElement.setAttribute("name", channel->get_name());
        chanElement.setAttribute("type", channel->get_type() == AudioChannel::ChannelIsInput ? "input" : "output");
        chanElement.setAttribute("id", channel->get_id());
        channelsElement.appendChild(chanElement);
    }
    projectNode.appendChild(channelsElement);

    QDomElement busesElement = doc.createElement("AudioBuses");

    QList<AudioBus*> buses;
    buses.append(m_hardwareAudioBuses);
    buses.append(m_softwareAudioBuses.values());

    foreach(AudioBus* bus, buses) {
        QDomElement busElement = doc.createElement("Bus");
        busElement.setAttribute("name", bus->get_name());
        busElement.setAttribute("channels", bus->get_channel_names().join(";"));
        QStringList channelIds;
        foreach(qint64 id, bus->get_channel_ids()) {
            channelIds.append(QString::number(id));
        }
        busElement.setAttribute("channelids", channelIds.join(";"));
        busElement.setAttribute("type", bus->is_input() ? "input" : "output");
        busElement.setAttribute("id", bus->get_id());
        busElement.setAttribute("bustype", bus->get_bus_type() == AudioBus::BusIsHardware ? "hardware" : "software");

        busesElement.appendChild(busElement);
    }

    projectNode.appendChild(busesElement);


    QDomNode busTracksNode = doc.createElement("BusTracks");
    foreach(TBusTrack* busTrack, m_busTracks.gui_items()) {
        busTracksNode.appendChild(busTrack->get_state(doc, istemplate));
    }

    projectNode.appendChild(busTracksNode);


    QDomNode masterOutNode = doc.createElement("MasterOut");
    masterOutNode.appendChild(m_masterOutBusTrack->get_state(doc, istemplate));
    projectNode.appendChild(masterOutNode);


    doc.appendChild(projectNode);

    // Get the AudioSources Node, and append
    if (! istemplate) {
        projectNode.appendChild(m_resourcesManager->get_state(doc));
    }

    // Get all the Sheets
    QDomNode sheetsNode = doc.createElement("Sheets");

    foreach(TSheet* sheet, m_sheets.gui_items()) {
        sheetsNode.appendChild(sheet->get_state(doc, istemplate));
    }

    projectNode.appendChild(sheetsNode);

    QDomNode workSheetsNode = doc.createElement("WorkSheets");
    foreach(TSession* session, m_childSessions) {
        workSheetsNode.appendChild(session->get_state(doc));
    }

    projectNode.appendChild(workSheetsNode);


    return projectNode;
}

void TProject::read_audio_device_setup(const QDomDocument& doc)
{
    TAudioDeviceSetup audioDeviceSetup;

    QDomNode audioDriverConfigurations = doc.documentElement().firstChildElement("AudioDriverConfigurations");
    QDomNode audioConfigurationNode = audioDriverConfigurations.firstChildElement("AudioDriverConfiguration");

    QDomElement e = audioConfigurationNode.toElement();
    audioDeviceSetup.set_project_name(m_name);
    audioDeviceSetup.set_driver_type(e.attribute("driver", ""));
    audioDeviceSetup.set_card_device(e.attribute("device", ""));
    audioDeviceSetup.set_sample_rate(e.attribute("samplerate", "44100").toUInt());
    audioDeviceSetup.set_buffer_size(e.attribute("buffersize", "1024").toUInt());
    //        audioDeviceSetup.jackChannels.append(m_softwareAudioChannels.values());

    if (audioDeviceSetup.get_driver_type().isEmpty() || audioDeviceSetup.get_driver_type().isNull()) {
#if defined (Q_OS_LINUX)
        audioDeviceSetup.set_driver_type(config().get_property("Hardware", "drivertype", "ALSA").toString());
#else
        audioDeviceSetup.set_driver_type(config().get_property("Hardware", "drivertype", "PortAudio").toString());
#endif
    }
    audioDeviceSetup.set_dither_shape(config().get_property("Hardware", "DitherShape", "None").toString());
    audioDeviceSetup.set_capture(config().get_property("Hardware", "capture", 1).toInt());
    audioDeviceSetup.set_playback(config().get_property("Hardware", "playback", 1).toInt());

    if (audioDeviceSetup.get_driver_type().isEmpty()) {
        qWarning("Driver type read from Settings is an empty String !!!");
        audioDeviceSetup.set_driver_type("ALSA");
    }

#if defined (ALSA_SUPPORT)
    if (audioDeviceSetup.get_driver_type() == "ALSA") {
        if (audioDeviceSetup.get_card_device().isEmpty()) {
            audioDeviceSetup.set_card_device(config().get_property("Hardware", "carddevice", "default").toString());
        }
    }
#endif

#if defined (PORTAUDIO_SUPPORT)
    if (audioDeviceSetup.get_driver_type() == "PortAudio") {
        if (audioDeviceSetup.get_card_device().isEmpty()) {
#if defined (Q_OS_LINUX)
            audioDeviceSetup.set_card_device(config().get_property("Hardware", "pahostapi", "alsa").toString());
#elif defined (Q_OS_MAC)
            audioDeviceSetup.set_card_device(config().get_property("Hardware", "pahostapi", "coreaudio").toString());
#elif defined (Q_OS_WIN)
            audioDeviceSetup.set_card_device(config().get_property("Hardware", "pahostapi", "wmme").toString());
#endif
        }
    }
#endif // end PORTAUDIO_SUPPORT

    m_audioDeviceSetup = audioDeviceSetup;
}

void TProject::set_project_closed()
{
    PENTER;
    m_projectClosed = true;

    // Closing a project could mean Traverso falls back to the Dummy driver
    // Loading Dummy driver causes driverParamsChanged to fire but we're closing
    // so definitely don't want to act on this signal anymore!
    disconnect(&audiodevice(), &TAudioDevice::driverParamsChanged, this, &TProject::audiodevice_params_changed);

    disconnect_from_audio_device();
}

void TProject::connect_to_audio_device()
{
    PENTER;
    audiodevice().add_client(m_audiodeviceClient);
}

// 'Blocking' call till we are disconnected from the AudioDevice
int TProject::disconnect_from_audio_device()
{
    PENTER;
    audiodevice().remove_client(m_audiodeviceClient);

    // Wait maximum 1 second
    int counter = 200;

    while (m_audiodeviceClient->is_connected() && counter > 0) {
        QThread::msleep(5);
        counter--;
    }

    if (counter == 0) {
        QMessageBox::critical( nullptr,
                                 tr("Traverso DAW - Malfunction"),
                                 tr("Unable to disconnect from Audio Device.\nClosing the application to free the Audio Device"),
                                 QMessageBox::Ok);
        qFatal("Could not disconnected AudioDeviceClient from AudioDevice. Closing the application");
    }

    return 1;
}

void TProject::add_meter(TAudioPlugin *meter)
{
    SpectralMeter* sm = qobject_cast<SpectralMeter*>(meter);
    if (sm) {
        m_spectralMeter = sm;
    }
    CorrelationMeter* cm = qobject_cast<CorrelationMeter*>(meter);
    if (cm) {
        m_correlationMeter = cm;
    }
}

/**
 * Get the Playback AudioBus instance with name \a name.

 * You can use this for example in your callback function to get a Playback Bus,
 * and mix audiodata into the Buses' buffers.
 * \sa get_playback_buses_names(), AudioBus::get_buffer()
 *
 * @param name The name of the Playback Bus
 * @return An AudioBus if one exists with name \a name, 0 on failure
 */
AudioBus* TProject::get_playback_bus(const QString& name) const
{
    foreach(AudioBus* bus, m_hardwareAudioBuses) {
        if (bus->get_type() == AudioChannel::ChannelIsOutput) {
            if (bus->get_name() == name) {
                return bus;
            }
        }
    }

    return nullptr;
}

/**
 * Get the Capture AudioBus instance with name \a name.

 * You can use this for example in your callback function to get a Capture Bus,
 * and read the audiodata from the Buses' buffers.
 * \sa AudioBus::get_buffer(),  get_capture_buses_names()
 *
 * @param name The name of the Capture Bus
 * @return An AudioBus if one exists with name \a name, 0 on failure
 */
AudioBus* TProject::get_capture_bus(const QString& name) const
{
    QList<AudioBus*> allBuses;
    allBuses.append(m_hardwareAudioBuses);
    allBuses.append(m_softwareAudioBuses.values());

    foreach(AudioBus* bus, allBuses) {
        if (bus->get_type() == AudioChannel::ChannelIsInput) {
            if (bus->get_name() == name) {
                return bus;
            }
        }
    }

    return nullptr;
}

AudioBus* TProject::get_audio_bus(qint64 id)
{
    if (m_masterOutBusTrack->get_id() == id) {
        return m_masterOutBusTrack->get_process_bus();
    }

    foreach(TSheet* sheet, m_sheets.gui_items()) {
        if (sheet->get_master_out_bus_track()->get_id() == id) {
            return sheet->get_master_out_bus_track()->get_process_bus();
        }
        foreach(TBusTrack* group, sheet->get_bus_tracks()) {
            if (group->get_id() == id) {
                return group->get_process_bus();
            }
        }
    }

    foreach(AudioBus* bus, m_hardwareAudioBuses) {
        if (bus->get_id() == id) {
            return bus;
        }
    }

    foreach(AudioBus* bus, m_softwareAudioBuses) {
        if (bus->get_id() == id) {
            return bus;
        }
    }

    foreach(TBusTrack* group, get_bus_tracks()) {
        if (group->get_id() == id) {
            return group->get_process_bus();
        }
    }

    return nullptr;
}

AudioBus* TProject::create_software_audio_bus(const TAudioBusConfiguration& conf)
{
    AudioBus* bus = new AudioBus(conf);

    AudioChannel* channel;
    for (int i=0; i< conf.channelNames.size(); ++i) {
        channel = new AudioChannel(conf.channelNames.at(i), uint(i), bus->get_type(), audiodevice().get_buffer_size());

        audiodevice().add_jack_channel(channel);
        bus->add_channel(channel);

        m_softwareAudioChannels.insert(channel->get_id(), channel);
    }


    m_softwareAudioBuses.insert(bus->get_id(), bus);

    return bus;
}

void TProject::remove_software_audio_bus(AudioBus *bus)
{
    for(uint i=0; i<bus->get_channel_count(); ++i) {
        AudioChannel* chan = bus->get_channel(i);
        audiodevice().remove_jack_channel(chan);
    }
}

QList<TSend*> TProject::get_inputs_for_bus_track(TBusTrack *busTrack) const
{
    QList<TSend*> inputs;

    QList<TTrack*> tracks;
    tracks.append(get_tracks());
    foreach(TSheet* sheet, m_sheets.gui_items()) {
        tracks.append(sheet->get_tracks());
        tracks.append(sheet->get_master_out_bus_track());
    }

    foreach(TTrack* track, tracks) {
        QList<TSend*> sends = track->get_post_sends();
        foreach(TSend* send, sends) {
            if (send->get_bus_id() == busTrack->get_id()) {
                inputs.append(send);
            }
        }
    }

    return inputs;
}

QList<TTrack*> TProject::get_sheet_tracks() const
{
    QList<TTrack*> tracks;
    foreach(TSheet* sheet, m_sheets.gui_items()) {
        tracks.append(sheet->get_tracks());
        tracks.append(sheet->get_master_out_bus_track());
    }
    return tracks;
}

TTrack* TProject::get_track(qint64 id) const
{
    QList<TTrack*> tracks = get_sheet_tracks();
    tracks.append(get_tracks());
    foreach(TTrack* track, tracks) {
        if (track->get_id() == id) {
            return track;
        }
    }
    return nullptr;
}


void TProject::track_property_changed()
{
    emit trackPropertyChanged();
}

/**
 * Get the names of all the Capture Buses availble, use the names to get a Bus instance
 * via get_capture_bus()
 *
 * @return A QStringList with all the Capture Buses names which are available,
 *		an empty list if no Buses are available.
 */
QStringList TProject::get_capture_buses_names( ) const
{
    QStringList names;
    foreach(AudioBus* bus, m_hardwareAudioBuses) {
        if (bus->get_type() == AudioChannel::ChannelIsInput) {
            names.append(bus->get_name());
        }
    }
    return names;
}

/**
 * Get the names of all the Playback Buses availble, use the names to get a Bus instance
 * via get_playback_bus()
 *
 * @return A QStringList with all the PlayBack Buses names which are available,
 *		an empty list if no Buses are available.
 */
QStringList TProject::get_playback_buses_names( ) const
{
    QStringList names;
    foreach(AudioBus* bus, m_hardwareAudioBuses) {
        if (bus->get_type() == AudioChannel::ChannelIsOutput) {
            names.append(bus->get_name());
        }
    }
    return names;
}

QList<AudioBus*> TProject::get_hardware_buses() const
{
    QList<AudioBus*> list;
    foreach(AudioBus* bus, m_hardwareAudioBuses) {
        if (bus->get_channel_names().count() == 1) {
            list.append(bus);
        }
    }
    foreach(AudioBus* bus, m_hardwareAudioBuses) {
        if (bus->get_channel_names().count() == 2) {
            list.append(bus);
        }
    }

    return list;
}

void TProject::set_title(const QString& title)
{
    if (title == m_name) {
        // do nothing if the title is the same as the current one
        return;
    }

    if (pm().project_exists(title)) {
        tInformUser().critical(tr("Project with title '%1' allready exists, not setting new title!").arg(title));
        return;
    }

    QString newrootdir = config().get_property("Project", "directory", "/directory/unknown/").toString() + "/" + title;

    QDir dir(m_rootDir);

    if ( ! dir.exists() ) {
        tInformUser().critical(tr("Project directory %1 no longer exists, did you rename it? "
                           "Shame on you! Please undo that, and come back later to rename your Project...").arg(m_rootDir));
        return;
    }

    m_name = title;

    save();

    if (pm().rename_project_dir(m_rootDir, newrootdir) < 0 ) {
        return;
    }

    QMessageBox::information( nullptr,
                             tr("Traverso - Information"),
                             tr("Project title changed, Project will to be reloaded to ensure proper operation"),
                             QMessageBox::Ok);

    pm().load_renamed_project(m_name);
}


void TProject::set_engineer(const QString& pEngineer)
{
    engineer=pEngineer;
}

void TProject::set_description(const QString& des)
{
    m_description = des;
}

void TProject::set_discid(const QString& pId)
{
    m_discid = pId;
}

void TProject::set_performer(const QString& pPerformer)
{
    m_performer = pPerformer;
}

void TProject::set_arranger(const QString& pArranger)
{
    m_arranger = pArranger;
}

void TProject::set_songwriter(const QString& sw)
{
    m_songwriter = sw;
}

void TProject::set_message(const QString& pMessage)
{
    m_message = pMessage;
}

void TProject::set_upc_ean(const QString& pUpc)
{
    m_upcEan = pUpc;
}

void TProject::set_genre(int pGenre)
{
    m_genre = pGenre;
}

bool TProject::has_changed()
{
    foreach(TSheet* sheet, m_sheets.gui_items()) {
        if(sheet->is_changed())
            return true;
    }
    return false;
}

TCommand* TProject::add_sheet(TSheet* sheet, bool historable)
{
    PENTER;

    return new TAddRemoveCommand(
        this,
        sheet,
        historable,
        [this, sheet](TRtTransaction& transaction) { private_add_sheet(sheet, transaction); },
        [this, sheet](TRtTransaction& transaction) { private_remove_sheet(sheet, transaction); },
        tr("Sheet %1 added").arg(sheet->get_name())
        );
}

TCommand* TProject::remove_sheet(TSheet* sheet, bool historable)
{
    return new TAddRemoveCommand(
        this,
        sheet,
        historable,
        [this, sheet](TRtTransaction& transaction) { private_remove_sheet(sheet, transaction); },
        [this, sheet](TRtTransaction& transaction) { private_add_sheet(sheet, transaction); },
        tr("Remove Sheet %1").arg(sheet->get_name())
        );
}



TSheet* TProject::get_sheet(qint64 id) const
{
    TSheet* current = nullptr;

    foreach(TSheet* sheet, m_sheets.gui_items()) {
        if (sheet->get_id() == id) {
            current = sheet;
            break;
        }
    }

    return current;
}

TSession* TProject::get_session(qint64 id)
{
    QList<TSession*> sessions = get_sessions();

    foreach(TSession* session, sessions) {
        if (session->get_id() == id) {
            return session;
        }
    }

    return nullptr;
}

void TProject::set_current_session(qint64 id)
{
    PENTER;

    // check if this session is allready current
    if (m_activeSession && m_activeSession->get_id() == id) {
        emit sessionIsAlreadyCurrent(m_activeSession);
        return;
    }

    TSession* session = get_session(id);

    if (!session) {
        qDebug("id %lld doesn't match any known sessions???", id);
        return;
    }

    m_activeSession = session;

    if (m_activeSession) {
        m_activeSessionId = m_activeSession->get_id();
    }

    TSheet* sheet = qobject_cast<TSheet*>(m_activeSession);

    if (sheet && (m_activeSheet != sheet)) {
        m_activeSheet = sheet;
        m_activeSheetId = sheet->get_id();
        set_parent_session(m_activeSheet);
    }


    emit currentSessionChanged(m_activeSession);
}

TSession* TProject::get_current_session() const
{
    return m_activeSession;
}


/* call this function to initiate the export or cd-writing */
int TProject::export_project()
{
    PENTER;

    m_exportSpecification->prepare_export(this);

    for (TSheet* sheet : m_exportSpecification->get_sheets_to_export()) {

        emit m_exportSpecification->exportMessage(QString("Starting export of %1").arg(sheet->get_name()));

        m_exportSpecification->resumeTransportLocation = sheet->get_transport_location();
        m_exportSpecification->resumeTransport = sheet->is_transport_rolling();
        if (sheet->is_transport_rolling()) {
            sheet->start_transport();
        }
        m_exportSpecification->set_export_file_name("Sheet_" + QString::number(get_sheet_index(sheet->get_id())) +"-" + sheet->get_name());

        TTimeRef exportStartLocation, exportEndLocation;
        bool exportRangeAvailable = false;

        if (m_exportSpecification->is_cd_export()) {
            exportRangeAvailable = sheet->get_cd_export_range(exportStartLocation, exportEndLocation);
        } else {
            exportRangeAvailable = sheet->get_export_range(exportStartLocation, exportEndLocation);
        }

        if (!exportRangeAvailable) {
            TLOG_W("export", "no export range for sheet {}", sheet->get_name());
            return -1;
        }

        m_exportSpecification->set_export_start_location(exportStartLocation);
        m_exportSpecification->set_export_end_location(exportEndLocation);

        // ... then start the render process and wait until it's finished
        sheet->set_transport_location(m_exportSpecification->get_export_start_location());

        while (sheet->get_transport_location() != m_exportSpecification->get_export_start_location()) {
            TLOG_D("export", "waiting for the sheet's seek to finish");
            usleep(100 * 1000);
            qApp->processEvents();
        }

        TLOG_D("export", "the sheet finished seeking");

        sheet->set_recordable_and_start_transport();

        emit m_exportSpecification->exportMessage(QString("Starting export of %1").arg(sheet->get_name()));
        m_exportSpecification->print_export_data();

        do {
            nframes_t diff = m_exportSpecification->get_remaining_export_frames();
            nframes_t nframes = std::min(diff, m_exportSpecification->get_block_size());

            // sheet->process(nframes);
            m_exportSpecification->add_exported_range(TTimeRef(nframes, audiodevice().get_sample_rate()));
        } while(!m_exportSpecification->cancel_export_requested() && m_exportSpecification->get_remaining_export_frames() > 0);

        sheet->start_transport();

        emit m_exportSpecification->exportMessage(QString("Finished export of %1").arg(sheet->get_name()));


        sheet->set_transport_location(m_exportSpecification->resumeTransportLocation);
        if (m_exportSpecification->resumeTransport) {
            sheet->start_transport();
        }

    }

    emit exportFinished();

    return 0;
}

void TProject::export_finished()
{
    // connect_to_audio_device();
}

void TProject::audio_device_removed_client(TAudioDeviceClient *client)
{
    PENTER;

    if (client != m_audiodeviceClient) {
        return;
    }


    if (m_projectClosed) {
        deleteLater();
    }

}

TExportSpecification *TProject::get_export_specification()
{
    if (!m_exportSpecification) {
        m_exportSpecification = new TExportSpecification();
        m_exportSpecification->set_export_dir(get_root_dir() + "/Export/");
        m_exportSpecification->set_block_size(audiodevice().get_buffer_size());
    }

    return m_exportSpecification;
}

uint TProject::project_rate() const
{
    return audiodevice().get_sample_rate();
}

uint TProject::get_rate( ) const
{
    // FIXME: Projects should eventually just use the universal samplerate
    if (m_useResampling) {
        return audiodevice().get_sample_rate();
    }

    return m_rate;
}

uint TProject::get_bitdepth( ) const
{
    return m_bitDepth;
}

void TProject::discard_undone_recordings()
{
    for (TSheet* sheet : get_sheets()) {
        sheet->discard_undone_recordings();
    }
}

QList<TSheet* > TProject::get_sheets( ) const
{
    return m_sheets.gui_items();
}

int TProject::get_sheet_index(qint64 id)
{
    const QList<TSheet*>& sheets = m_sheets.gui_items();
    for (int i=0; i<sheets.size(); ++i) {
        TSheet* sheet = sheets.at(i);
        if (sheet->get_id() == id) {
            return i + 1;
        }
    }

    return 0;
}

int TProject::get_session_index(qint64 id)
{
    QList<TSession*> sessions = get_sessions();

    for(int i=0; i<sessions.size(); ++i) {
        if (sessions.at(i)->get_id() == id) {
            return i;
        }
    }

    return -1;
}

QList<TSession*> TProject::get_sessions()
{
    QList<TSession*> sessions;
    sessions.append(this);
    sessions.append(get_child_sessions());
    foreach(TSheet* sheet, m_sheets.gui_items()) {
        sessions.append(sheet);
        foreach(TSession* session, sheet->get_child_sessions()) {
            sessions.append(session);
        }
    }

    return sessions;
}

qint64 TProject::get_current_sheet_id( ) const
{
    return m_activeSheetId;
}

int TProject::get_num_sheets( ) const
{
    return m_sheets.gui_items().size();
}

QString TProject::get_title( ) const
{
    return m_name;
}

QString TProject::get_engineer( ) const
{
    return engineer;
}

QString TProject::get_description() const
{
    return m_description;
}

QString TProject::get_discid() const
{
    return m_discid;
}

QString TProject::get_performer() const
{
    return m_performer;
}

QString TProject::get_arranger() const
{
    return m_arranger;
}

QString TProject::get_songwriter() const
{
    return m_songwriter;
}

QString TProject::get_message() const
{
    return m_message;
}

QString TProject::get_upc_ean() const
{
    return m_upcEan;
}

int TProject::get_genre()
{
    return m_genre;
}

QString TProject::get_root_dir( ) const
{
    return m_rootDir;
}

QString TProject::get_audiosources_dir() const
{
    return m_rootDir + "/audiosources/";
}

TResourcesManager * TProject::get_audiosource_manager( ) const
{
    return m_resourcesManager;
}

void TProject::audiodevice_params_changed()
{
    PENTER;

    // What the device runs now is what the project saves, e.g. after a
    // driver change in the settings or a fallback to the Dummy driver
    m_audioDeviceSetup = audiodevice().get_device_setup();

    setup_default_hardware_buses();

    for (TSheet* sheet : m_sheets.gui_items()) {
        sheet->audiodevice_params_changed();
    }

    for(AudioBus* bus : std::as_const(m_hardwareAudioBuses)) {
        bus->audiodevice_params_changed();
    }

    uint bufferSize = audiodevice().get_buffer_size();
    for(AudioChannel* channel : std::as_const(m_softwareAudioChannels)) {
        channel->set_buffer_size(bufferSize);
    }
}

void TProject::setup_default_hardware_buses()
{
    int number = 1;

    TAudioBusConfiguration config;
    config.type = "input";
    config.bustype = "hardware";

    QList<AudioChannel*> channels = audiodevice().get_capture_channels();
    for (int i=0; i < channels.size();) {
        config.name = "Capture " + QByteArray::number(number) + "-" + QByteArray::number(number + 1);
        number += 2;
        AudioBus* exists = get_capture_bus(config.name);
        if (exists) {
            // don't add a hardware bus with the same name, so continue:
            i+=2;
            continue;
        }
        AudioBus* bus = new AudioBus(config);
        bus->add_channel("capture_"+QByteArray::number(1 + i++));
        bus->add_channel("capture_"+QByteArray::number(1 + i++));

        m_hardwareAudioBuses.append(bus);
    }

    for (int i=0; i < channels.size(); ++i) {
        config.name = "Capture " + QByteArray::number(i + 1);
        AudioBus* exists = get_capture_bus(config.name);
        if (exists) {
            // don't add a hardware bus with the same name, so continue:
            continue;
        }
        AudioBus* bus = new AudioBus(config);
        bus->add_channel("capture_"+QByteArray::number(i + 1));
        m_hardwareAudioBuses.append(bus);
    }

    number = 1;

    channels = audiodevice().get_playback_channels();
    config.type = "output";

    for (int i=0; i < channels.size();) {
        config.name = "Playback " + QString::number(number) + "-" + QByteArray::number(number + 1);
        number += 2;
        AudioBus* exists = get_playback_bus(config.name);
        if (exists) {
            // don't add a hardware bus with the same name, so continue:
            i+=2;
            continue;
        }
        AudioBus* bus = new AudioBus(config);
        bus->add_channel("playback_"+QByteArray::number(1 + i++));
        bus->add_channel("playback_"+QByteArray::number(1 + i++));
        m_hardwareAudioBuses.append(bus);
    }

    for (int i=0; i < channels.size(); ++i) {
        config.name = "Playback " + QByteArray::number(i + 1);
        AudioBus* exists = get_playback_bus(config.name);
        if (exists) {
            // don't add a hardware bus with the same name, so continue:
            continue;
        }
        AudioBus* bus = new AudioBus(config);
        bus->add_channel("playback_"+QByteArray::number(i + 1));
        m_hardwareAudioBuses.append(bus);
    }
}

void TProject::private_add_sheet(TSheet* sheet, TRtTransaction& transaction)
{
    PENTER;
    m_sheets.add(sheet, transaction);
    emit sheetAdded(sheet);
}

void TProject::private_remove_sheet(TSheet* sheet, TRtTransaction& transaction)
{
    PENTER;
    m_sheets.remove(sheet, transaction);

    if (!m_sheets.gui_items().isEmpty()) {
        set_current_session(m_sheets.gui_items().last()->get_id());
    }

    emit sheetRemoved(sheet);
}

QString TProject::get_import_dir() const
{
    QDir dir;
    if (!dir.exists(m_importDir)) {
        return QDir::homePath();
    }

    return m_importDir;
}

void TProject::set_import_dir(const QString& dir)
{
    m_importDir = dir;
}

bool TProject::is_save_to_close() const
{
    if (is_recording()) {
        QMessageBox::information( nullptr,
                                 tr("Traverso - Information"),
                                 tr("You're still recording, please stop recording first to be able to exit the application!"),
                                 QMessageBox::Ok);
        return false;
    }
    return true;
}

bool TProject::is_recording() const
{
    foreach(TSheet* sheet, m_sheets.gui_items()) {
        if (sheet->is_recording() && sheet->is_transport_rolling()) {
            return true;
        }
    }
    return false;
}

void TProject::set_work_at(const TTimeRef &worklocation, bool isFolder)
{
    foreach(TSheet* sheet, m_sheets.gui_items()) {
        sheet->set_work_at(worklocation, isFolder);
    }
}

void TProject::set_sheets_are_tracks_folder(bool isFolder)
{
    m_sheetsAreTrackFolder = isFolder;
    if (m_sheetsAreTrackFolder) {
        tInformUser().information(tr("Sheets behave as Tracks Folder"));
    } else {
        tInformUser().information(tr("Sheets NO longer behave as Tracks Folder"));
    }
}


int TProject::process(TProcessContext &context) T_REALTIME_SAFE
{
    int result = 0;
    nframes_t nframes = context.get_nframes_to_process();

    context.set_start_location(get_transport_location());

    for (TSheet* sheet : m_sheets.rt_items()) {
        result |= sheet->process(context);
    }


    for(TBusTrack* busTrack : m_busTracks.rt_items()) {
        busTrack->process(context);
    }


    // FIXME both Meter's are native plugins but are owned by their respective View's.
    // adding them to MasterOutBusTracks means when closing Project they are saved as
    // a plugin in the project file and so the project file becomes cluttered with
    // new meters every time a project is saved and when Project is closed the Meter objects
    // are destroyed in PluginChain destructor and so the Views have a dangling pointer.
    // The correct solution is to have GUI support for Plugins and dock the Plugin GUI
    // somewhere
    // To process them here is a temporary solution since the m_masterOutBusTrack->process()
    // is called after procssing the signal for the Meters hence we miss some signal processing.
    if (m_correlationMeter) {
        m_correlationMeter->process(m_masterOutBusTrack->get_process_bus(), nframes);
    }
    if (m_spectralMeter) {
        m_spectralMeter->process(m_masterOutBusTrack->get_process_bus(), nframes);
    }

    // Mix the result into the AudioDevice "physical" buffers
    m_masterOutBusTrack->process(context);

    // The schedulers are woken once per cycle, after the sheets took from
    // the rings and asked for their seeks; also with the transport stopped
    // (a seek, a writer that finishes its file after recording)
    m_readScheduler->tick();
    m_writeScheduler->tick();

    return result;
}

int TProject::transport_control(TTransportControl *transportControl) T_REALTIME_SAFE
{
    bool result = true;

    // Called in the audio thread when the transport follows jack, in the GUI
    // thread otherwise: each reads its own side of the list
    if (transportControl->is_realtime()) {
        for (TSheet* sheet : m_sheets.rt_items()) {
            result = sheet->transport_control(transportControl);
        }
    } else {
        for (TSheet* sheet : m_sheets.gui_items()) {
            result = sheet->transport_control(transportControl);
        }
    }

    return result;
}

TTimeRef TProject::get_last_location() const
{
    TTimeRef lastLocation;

    foreach(TSheet* sheet, m_sheets.gui_items()) {
        TTimeRef location = sheet->get_last_location();
        if (location > lastLocation) {
            lastLocation = location;
        }
    }

    return lastLocation;
}

TTimeRef TProject::get_transport_location() const T_REALTIME_SAFE
{
    if (!m_activeSheet) return TTimeRef();

    return m_activeSheet->get_transport_location();
}

TCommand* TProject::remove_child_session()
{
    PENTER;

    if (!m_activeSession) {
        return nullptr;
    }

    if (m_activeSession->is_project_session()) {
        // Oh no, we're not gonna delete project itself!
        return nullptr;
    }

    if (!m_activeSession->is_child_session()) {
        return nullptr;
    }

    TSession* toBeRemoved = m_activeSession;
    TSession* parentSession = m_activeSession->get_parent_session();

    parentSession->remove_child_session(toBeRemoved);

    set_current_session(parentSession->get_id());


    delete toBeRemoved;

    return nullptr;
}
