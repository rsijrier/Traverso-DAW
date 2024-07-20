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

#include <QFile>
#include <QDir>
#include <QTextStream>
#include <QMessageBox>
#include <QString>

#include <unistd.h>

#include "AudioBus.h"
#include "AudioChannel.h"
#include "AudioTrack.h"
#include "TAudioDeviceClient.h"
#include "Project.h"
#include "Sheet.h"
#include "ProjectManager.h"
#include "Information.h"
#include "TExportThread.h"
#include "TInputEventDispatcher.h"
#include "ResourcesManager.h"
#include "TExportSpecification.h"
#include "AudioDevice.h"
#include "TConfig.h"
#include "ContextPointer.h"
#include "Utils.h"
#include <AddRemove.h>
#include "FileHelpers.h"
#include "TTimeLineRuler.h"
#include "TBusTrack.h"
#include "TSend.h"
#include "SpectralMeter.h"
#include "CorrelationMeter.h"

#define PROJECT_FILE_VERSION 	3
#define MASTER_OUT_SOFTWARE_BUS_ID 1
// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

/**	\class Project
	\brief Project restores and saves the state of a Traverso Project
	
	A Project can have as much Sheet's as one likes. A Project with one Sheet acts like a 'Session'
	where the Sheet can be turned into a CD with various Tracks using Marker 's
	When a Project has multiple Sheet's, each Sheet will be a CD Track, this can be usefull if each
	Track on a CD is independend of the other CD Tracks.
 
 */


Project::Project(const QString& title)
    : TSession()
{
    PENTERCONS;
    m_name = title;
    m_exportThread = nullptr;
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

    m_resourcesManager = new ResourcesManager(this);
    create_history_stack();

    m_audiodeviceClient = new TAudioDeviceClient("sheet_" + QByteArray::number(get_id()));
    m_audiodeviceClient->set_process_callback( MakeDelegate(this, &Project::process) );
    m_audiodeviceClient->set_transport_control_callback( MakeDelegate(this, &Project::transport_control) );

    m_disconnectAudioDeviceClientForExport = false;
    m_exportSpecification = nullptr;

    m_masterOutBusTrack = new MasterOutSubGroup(this, "");
    // FIXME: m_masterOut is a Track, but at this point in time, Track can't
    // get a reference to us via pm().get_project();
    connect(m_masterOutBusTrack, SIGNAL(routingConfigurationChanged()), this, SLOT(track_property_changed()));


    AudioBus* bus = m_masterOutBusTrack->get_process_bus();
    for(uint i=0; i<bus->get_channel_count(); i++) {
        if (AudioChannel* chan = bus->get_channel(i)) {
            chan->set_buffer_size(2048);
        }
    }

    cpointer().add_contextitem(this);

    connect(this, SIGNAL(privateSheetRemoved(Sheet*)), this, SLOT(sheet_removed(Sheet*)));
    connect(this, SIGNAL(privateSheetAdded(Sheet*)), this, SLOT(sheet_added(Sheet*)));
    connect(this, SIGNAL(exportFinished()), this, SLOT(export_finished()), Qt::QueuedConnection);
    connect(&audiodevice(), SIGNAL(driverParamsChanged()), this, SLOT(audiodevice_params_changed()), Qt::DirectConnection);
    connect(&audiodevice(),SIGNAL(audioDeviceClientRemoved(TAudioDeviceClient*)), this, SLOT(audio_device_removed_client(TAudioDeviceClient*)));
}


Project::~Project()
{
    PENTERDES;

    cpointer().remove_contextitem(this);

    delete m_resourcesManager;

    foreach(Sheet* sheet, m_sheets) {
        delete sheet;
    }

    delete m_masterOutBusTrack;
}


int Project::create(int sheetcount, int numtracks)
{
    PENTER;
    PMESG("Creating new project %s  NumSheets=%d", QS_C(m_name), sheetcount);

    QDir dir;
    if (!dir.mkdir(m_rootDir)) {
        info().critical(tr("Cannot create dir %1").arg(m_rootDir));
        return -1;
    }

    if (create_peakfiles_dir() < 0) {
        return -1;
    }

    if (create_audiosources_dir() < 0) {
        return -1;
    }

    if (pm().create_projectfilebackup_dir(m_rootDir) < 0) {
        return -1;
    }

    for (int i=0; i< sheetcount; i++) {
        Sheet* sheet = new Sheet(this, numtracks);
        m_RtSheets.append(sheet);
        m_sheets.append(sheet);
    }

    if (m_sheets.size()) {
        set_current_session(m_sheets.first()->get_id());
    }

    m_importDir = QDir::homePath();

    // TODO: by calling prepare_audio_device() with an empty document
    // all the defaults from the global config are applied to this projects
    // audio device setup. It means audiodevice gets started/stopped twice
    // once for creating the project, once for loading the project afterwards.
    // should be handled more galantly?
    QDomDocument doc;
    prepare_audio_device(doc);

    info().information(tr("Created new Project %1").arg(m_name));
    return 1;
}

int Project::create_audiosources_dir()
{
    QDir dir;
    if (!dir.mkdir(m_sourcesDir)) {
        info().critical(tr("Cannot create dir %1").arg(m_sourcesDir));
        return -1;
    }

    return 1;

}

int Project::create_peakfiles_dir()
{
    QDir dir;
    QString peaksDir = m_rootDir + "/peakfiles/";

    if (!dir.mkdir(peaksDir)) {
        info().critical(tr("Cannot create dir %1").arg(peaksDir));
        return -1;
    }

    return 1;
}


int Project::load(const QString& projectfile)
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
        info().critical(m_errorString);
        return PROJECT_FILE_COULD_NOT_BE_OPENED;
    }

    // Check if important directories still exist!
    QDir dir;
    if (!dir.exists(m_rootDir + "/peakfiles")) {
        create_peakfiles_dir();
    }
    if (!dir.exists(m_rootDir + "/audiosources")) {
        create_audiosources_dir();
    }


    // Start setting and parsing the content of the xml file
    QString errorMsg;
    if (!doc.setContent(&file, &errorMsg)) {
        m_errorString = tr("Project %1: Failed to parse project.tpf file! (Reason: %2)").arg(m_name).arg(errorMsg);
        info().critical(m_errorString);
        return SETTING_XML_CONTENT_FAILED;
    }

    emit projectLoadStarted();

    QDomElement docElem = doc.documentElement();
    QDomNode propertiesNode = docElem.firstChildElement("Properties");
    QDomElement e = propertiesNode.toElement();

    if (e.attribute("projectfileversion", "-1").toInt() != PROJECT_FILE_VERSION) {
        m_errorString = tr("Project File Version does not match, unable to load Project!");
        info().warning(m_errorString);
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
    m_sheetsAreTrackFolder = e.attribute("sheetsaretrackfolder", "0").toInt();
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

        if (bus->get_bus_type() == AudioBus::BusIsSoftware) {
            AudioChannel* channel;
            foreach(QString idString, channelIds) {
                qint64 id = idString.toLongLong();
                channel = m_softwareAudioChannels.value(id);
                if (channel) {
                    bus->add_channel(channel);
                }
            }
            m_softwareAudioBuses.insert(bus->get_id(), bus);
        }
        if (bus->get_bus_type() == AudioBus::BusIsHardware) {
            foreach(QString channelName, conf.channelNames) {
                bus->add_channel(channelName);
            }
            m_hardwareAudioBuses.append(bus);
        }

        busNode = busNode.nextSibling();
    }


    prepare_audio_device(doc);

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

    while(!busTrackNode.isNull()) {
        TBusTrack* busTrack = new TBusTrack(this, busTrackNode);
        busTrack->set_state(busTrackNode);
        private_add_track(busTrack);
        private_track_added(busTrack);

        busTrackNode = busTrackNode.nextSibling();
    }

    // Load all the AudioSources for this project
    QDomNode asmNode = docElem.firstChildElement("ResourcesManager");
    m_resourcesManager->set_state(asmNode);


    QDomNode sheetsNode = docElem.firstChildElement("Sheets");
    QDomNode sheetNode = sheetsNode.firstChild();

    // Load all the Sheets
    while(!sheetNode.isNull())
    {
        Sheet* sheet = new Sheet(this, sheetNode);
        // add it to the non-real time safe list
        m_sheets.append(sheet);
        // and to the real time safe list
        m_RtSheets.append(sheet);

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
        if (m_sheets.size()) {
            activeSheetId = m_sheets.first()->get_id();
        }
    }

    if (activeSheetId == activeSessionId) {
        set_current_session(activeSheetId);
    } else {
        set_current_session(activeSheetId);
        set_current_session(activeSessionId);
    }

    info().information( tr("Project %1 loaded").arg(m_name) );

    emit projectLoadFinished();

    return 1;
}

int Project::save_from_template_to_project_file(const QString& templateFile, const QString& projectName)
{
    QFile file(templateFile);
    QString saveFileName = m_rootDir + "/project.tpf";

    QDomDocument doc("Project");

    if (!file.open(QIODevice::ReadOnly)) {
        m_errorString = tr("Project %1: Cannot open project.tpf file! (Reason: %2)").arg(m_name, file.errorString());
        info().critical(m_errorString);
        return PROJECT_FILE_COULD_NOT_BE_OPENED;
    }

    // Start setting and parsing the content of the xml file
    QString errorMsg;
    if (!doc.setContent(&file, &errorMsg)) {
        m_errorString = tr("Project %1: Failed to parse project.tpf file! (Reason: %2)").arg(m_name, errorMsg);
        info().critical(m_errorString);
        return SETTING_XML_CONTENT_FAILED;
    }

    QDomElement docElem = doc.documentElement();
    QDomNode propertiesNode = docElem.firstChildElement("Properties");
    QDomElement e = propertiesNode.toElement();

    if (e.attribute("projectfileversion", "-1").toInt() != PROJECT_FILE_VERSION) {
        m_errorString = tr("Project File Version does not match, unable to load Project!");
        info().warning(m_errorString);
        return PROJECT_FILE_VERSION_MISMATCH;
    }

    e.setAttribute("title", projectName);

    QFile data( saveFileName );

    if (!data.open( QIODevice::WriteOnly ) ) {
        QString errorstring = FileHelper::fileerror_to_string(data.error());
        info().critical( tr("Couldn't open Project properties file for writing! (File %1. Reason: %2)").arg(saveFileName).arg(errorstring) );
        return -1;
    }

    QTextStream stream(&data);
    doc.save(stream, 4);
    data.close();

    return 1;
}


int Project::save(bool autosave)
{
    PENTER;
    QDomDocument doc("Project");
    QString fileName = m_rootDir + "/project.tpf";

    QFile data( fileName );

    if (!data.open( QIODevice::WriteOnly ) ) {
        QString errorstring = FileHelper::fileerror_to_string(data.error());
        info().critical( tr("Couldn't open Project properties file for writing! (File %1. Reason: %2)").arg(fileName).arg(errorstring) );
        return -1;
    }

    get_state(doc);
    QTextStream stream(&data);
    doc.save(stream, 4);
    data.close();

    if (!autosave) {
        info().information( tr("Project %1 saved ").arg(m_name) );
    }

    pm().start_incremental_backup(this);

    return 1;
}


QDomNode Project::get_state(QDomDocument doc, bool istemplate)
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
    properties.setAttribute("sheetsaretrackfolder", m_sheetsAreTrackFolder);
    if (! istemplate) {
        properties.setAttribute("id", get_id());
    } else {
        properties.setAttribute("title", "Template Project File!!");
    }
    properties.setAttribute("importdir", m_importDir);

    projectNode.appendChild(properties);


    QDomElement audioDriverConfigs = doc.createElement("AudioDriverConfigurations");
    QDomElement audioDriverConfig = doc.createElement("AudioDriverConfiguration");

    audioDriverConfig.setAttribute("device", audiodevice().get_device_setup().cardDevice);
    audioDriverConfig.setAttribute("driver", audiodevice().get_device_setup().driverType);
    audioDriverConfig.setAttribute("samplerate", audiodevice().get_sample_rate());
    audioDriverConfig.setAttribute("buffersize", audiodevice().get_buffer_size());

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
    foreach(TBusTrack* busTrack, m_busTracks) {
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

    foreach(Sheet* sheet, m_sheets) {
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

void Project::prepare_audio_device(QDomDocument doc)
{
    TAudioDeviceSetup ads;

    QDomNode audioDriverConfigurations = doc.documentElement().firstChildElement("AudioDriverConfigurations");
    QDomNode audioConfigurationNode = audioDriverConfigurations.firstChildElement("AudioDriverConfiguration");

    QDomElement e = audioConfigurationNode.toElement();
    ads.driverType = e.attribute("driver", "");
    ads.cardDevice = e.attribute("device", "");
    ads.rate = e.attribute("samplerate", "44100").toUInt();
    ads.bufferSize = e.attribute("buffersize", "1024").toUInt();
    //        ads.jackChannels.append(m_softwareAudioChannels.values());

    if (ads.driverType.isEmpty() || ads.driverType.isNull()) {
#if defined (Q_OS_UNIX)
        ads.driverType = config().get_property("Hardware", "drivertype", "ALSA").toString();
#else
        ads.driverType = config().get_property("Hardware", "drivertype", "PortAudio").toString();
#endif
    }
    ads.ditherShape = config().get_property("Hardware", "DitherShape", "None").toString();
    ads.capture = config().get_property("Hardware", "capture", 1).toInt();
    ads.playback = config().get_property("Hardware", "playback", 1).toInt();

    if (ads.bufferSize == 0) {
        qWarning("BufferSize read from Settings is 0 !!!");
        ads.bufferSize = 1024;
    }
    if (ads.rate == 0) {
        qWarning("Samplerate read from Settings is 0 !!!");
        ads.rate = 44100;
    }
    if (ads.driverType.isEmpty()) {
        qWarning("Driver type read from Settings is an empty String !!!");
        ads.driverType = "ALSA";
    }

#if defined (ALSA_SUPPORT)
    if (ads.driverType == "ALSA") {
        if (ads.cardDevice.isEmpty()) {
            ads.cardDevice = config().get_property("Hardware", "carddevice", "default").toString();
        }
    }
#endif

#if defined (PORTAUDIO_SUPPORT)
    if (ads.driverType == "PortAudio") {
        if (ads.cardDevice.isEmpty()) {
#if defined (Q_OS_UNIX)
            ads.cardDevice = config().get_property("Hardware", "pahostapi", "alsa").toString();
#elif defined (Q_OS_MAC)
            ads.cardDevice = config().get_property("Hardware", "pahostapi", "coreaudio").toString();
#elif defined (Q_OS_WIN)
            ads.cardDevice = config().get_property("Hardware", "pahostapi", "wmme").toString();
#endif
        }
    }
#endif // end PORTAUDIO_SUPPORT


    audiodevice().set_parameters(ads);
}

void Project::connect_to_audio_device()
{
    audiodevice().add_client(m_audiodeviceClient);
}

int Project::disconnect_from_audio_device()
{
    PENTER;
    audiodevice().remove_client(m_audiodeviceClient);
    return 1;
}

void Project::add_meter(Plugin *meter)
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
AudioBus* Project::get_playback_bus(const QString& name) const
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
AudioBus* Project::get_capture_bus(const QString& name) const
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

AudioBus* Project::get_audio_bus(qint64 id)
{
    if (m_masterOutBusTrack->get_id() == id) {
        return m_masterOutBusTrack->get_process_bus();
    }

    foreach(Sheet* sheet, m_sheets) {
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

AudioBus* Project::create_software_audio_bus(const TAudioBusConfiguration& conf)
{
    AudioBus* bus = new AudioBus(conf);

    AudioChannel* channel;
    for (int i=0; i< conf.channelNames.size(); ++i) {
        channel = new AudioChannel(conf.channelNames.at(i), uint(i), bus->get_type());
        channel->set_buffer_size(audiodevice().get_buffer_size());

        audiodevice().add_jack_channel(channel);
        bus->add_channel(channel);

        m_softwareAudioChannels.insert(channel->get_id(), channel);
    }


    m_softwareAudioBuses.insert(bus->get_id(), bus);

    return bus;
}

void Project::remove_software_audio_bus(AudioBus *bus)
{
    for(uint i=0; i<bus->get_channel_count(); ++i) {
        AudioChannel* chan = bus->get_channel(i);
        audiodevice().remove_jack_channel(chan);
    }
}

QList<TSend*> Project::get_inputs_for_bus_track(TBusTrack *busTrack) const
{
    QList<TSend*> inputs;

    QList<Track*> tracks;
    tracks.append(get_tracks());
    foreach(Sheet* sheet, m_sheets) {
        tracks.append(sheet->get_tracks());
        tracks.append(sheet->get_master_out_bus_track());
    }

    foreach(Track* track, tracks) {
        QList<TSend*> sends = track->get_post_sends();
        foreach(TSend* send, sends) {
            if (send->get_bus_id() == busTrack->get_id()) {
                inputs.append(send);
            }
        }
    }

    return inputs;
}

QList<Track*> Project::get_sheet_tracks() const
{
    QList<Track*> tracks;
    foreach(Sheet* sheet, m_sheets) {
        tracks.append(sheet->get_tracks());
        tracks.append(sheet->get_master_out_bus_track());
    }
    return tracks;
}

Track* Project::get_track(qint64 id) const
{
    QList<Track*> tracks = get_sheet_tracks();
    tracks.append(get_tracks());
    foreach(Track* track, tracks) {
        if (track->get_id() == id) {
            return track;
        }
    }
    return nullptr;
}


void Project::track_property_changed()
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
QStringList Project::get_capture_buses_names( ) const
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
QStringList Project::get_playback_buses_names( ) const
{
    QStringList names;
    foreach(AudioBus* bus, m_hardwareAudioBuses) {
        if (bus->get_type() == AudioChannel::ChannelIsOutput) {
            names.append(bus->get_name());
        }
    }
    return names;
}

QList<AudioBus*> Project::get_hardware_buses() const
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

void Project::set_title(const QString& title)
{
    if (title == m_name) {
        // do nothing if the title is the same as the current one
        return;
    }

    if (pm().project_exists(title)) {
        info().critical(tr("Project with title '%1' allready exists, not setting new title!").arg(title));
        return;
    }

    QString newrootdir = config().get_property("Project", "directory", "/directory/unknown/").toString() + "/" + title;

    QDir dir(m_rootDir);

    if ( ! dir.exists() ) {
        info().critical(tr("Project directory %1 no longer exists, did you rename it? "
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


void Project::set_engineer(const QString& pEngineer)
{
    engineer=pEngineer;
}

void Project::set_description(const QString& des)
{
    m_description = des;
}

void Project::set_discid(const QString& pId)
{
    m_discid = pId;
}

void Project::set_performer(const QString& pPerformer)
{
    m_performer = pPerformer;
}

void Project::set_arranger(const QString& pArranger)
{
    m_arranger = pArranger;
}

void Project::set_songwriter(const QString& sw)
{
    m_songwriter = sw;
}

void Project::set_message(const QString& pMessage)
{
    m_message = pMessage;
}

void Project::set_upc_ean(const QString& pUpc)
{
    m_upcEan = pUpc;
}

void Project::set_genre(int pGenre)
{
    m_genre = pGenre;
}

bool Project::has_changed()
{
    foreach(Sheet* sheet, m_sheets) {
        if(sheet->is_changed())
            return true;
    }
    return false;
}


TCommand* Project::add_sheet(Sheet* sheet, bool historable)
{
    PENTER;

    AddRemove* cmd;
    cmd = new AddRemove(this, sheet, historable, nullptr,
                        "private_add_sheet(Sheet*)", "privateSheetAdded(Sheet*)",
                        "private_remove_sheet(Sheet*)", "privateSheetRemoved(Sheet*)",
                        tr("Sheet %1 added").arg(sheet->get_name()));

    return cmd;
}


TCommand* Project::remove_sheet(Sheet* sheet, bool historable)
{
    AddRemove* cmd;
    cmd = new AddRemove(this, sheet, historable, nullptr,
                        "private_remove_sheet(Sheet*)", "privateSheetRemoved(Sheet*)",
                        "private_add_sheet(Sheet*)", "privateSheetAdded(Sheet*)",
                        tr("Remove Sheet %1").arg(sheet->get_name()));


    return cmd;
}


Sheet* Project::get_sheet(qint64 id) const
{
    Sheet* current = nullptr;

    foreach(Sheet* sheet, m_sheets) {
        if (sheet->get_id() == id) {
            current = sheet;
            break;
        }
    }

    return current;
}

TSession* Project::get_session(qint64 id)
{
    QList<TSession*> sessions = get_sessions();

    foreach(TSession* session, sessions) {
        if (session->get_id() == id) {
            return session;
        }
    }

    return nullptr;
}

void Project::set_current_session(qint64 id)
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

    Sheet* sheet = qobject_cast<Sheet*>(m_activeSession);

    if (sheet && (m_activeSheet != sheet)) {
        m_activeSheet = sheet;
        m_activeSheetId = sheet->get_id();
        set_parent_session(m_activeSheet);
    }


    emit currentSessionChanged(m_activeSession);
}

TSession* Project::get_current_session() const
{
    return m_activeSession;
}


/* call this function to initiate the export or cd-writing */
int Project::export_project()
{
    PENTER;

    m_disconnectAudioDeviceClientForExport = true;

    disconnect_from_audio_device();

    return 0;
}

void Project::export_finished()
{
    connect_to_audio_device();
}

void Project::audio_device_removed_client(TAudioDeviceClient *client)
{
    PENTER;

    if (client != m_audiodeviceClient) {
        return;
    }

    if (m_disconnectAudioDeviceClientForExport) {
        m_exportSpecification->start_export(this);
        m_disconnectAudioDeviceClientForExport = false;
    }

    if (m_projectClosed) {
        deleteLater();
    }

}

TExportSpecification *Project::get_export_specification()
{
    if (!m_exportSpecification) {
        m_exportSpecification = new TExportSpecification();
        m_exportSpecification->set_export_dir(get_root_dir() + "/Export/");
        m_exportSpecification->set_block_size(audiodevice().get_buffer_size());
    }

    return m_exportSpecification;
}

uint Project::get_rate( ) const
{
    // FIXME: Projects should eventually just use the universal samplerate
    if (m_useResampling) {
        return audiodevice().get_sample_rate();
    }

    return m_rate;
}

uint Project::get_bitdepth( ) const
{
    return m_bitDepth;
}

QList<Sheet* > Project::get_sheets( ) const
{
    return m_sheets;
}

int Project::get_sheet_index(qint64 id)
{
    for (int i=0; i<m_sheets.size(); ++i) {
        Sheet* sheet = m_sheets.at(i);
        if (sheet->get_id() == id) {
            return i + 1;
        }
    }

    return 0;
}

int Project::get_session_index(qint64 id)
{
    QList<TSession*> sessions = get_sessions();

    for(int i=0; i<sessions.size(); ++i) {
        if (sessions.at(i)->get_id() == id) {
            return i;
        }
    }

    return -1;
}

QList<TSession*> Project::get_sessions()
{
    QList<TSession*> sessions;
    sessions.append(this);
    sessions.append(get_child_sessions());
    foreach(Sheet* sheet, m_sheets) {
        sessions.append(sheet);
        foreach(TSession* session, sheet->get_child_sessions()) {
            sessions.append(session);
        }
    }

    return sessions;
}

qint64 Project::get_current_sheet_id( ) const
{
    return m_activeSheetId;
}

int Project::get_num_sheets( ) const
{
    return m_sheets.size();
}

QString Project::get_title( ) const
{
    return m_name;
}

QString Project::get_engineer( ) const
{
    return engineer;
}

QString Project::get_description() const
{
    return m_description;
}

QString Project::get_discid() const
{
    return m_discid;
}

QString Project::get_performer() const
{
    return m_performer;
}

QString Project::get_arranger() const
{
    return m_arranger;
}

QString Project::get_songwriter() const
{
    return m_songwriter;
}

QString Project::get_message() const
{
    return m_message;
}

QString Project::get_upc_ean() const
{
    return m_upcEan;
}

int Project::get_genre()
{
    return m_genre;
}

QString Project::get_root_dir( ) const
{
    return m_rootDir;
}

QString Project::get_audiosources_dir() const
{
    return m_rootDir + "/audiosources/";
}

ResourcesManager * Project::get_audiosource_manager( ) const
{
    return m_resourcesManager;
}

void Project::audiodevice_params_changed()
{
    setup_default_hardware_buses();

    foreach(AudioBus* bus, m_hardwareAudioBuses) {
        bus->audiodevice_params_changed();
    }

    uint bufferSize = audiodevice().get_buffer_size();
    foreach(AudioChannel* channel, m_softwareAudioChannels) {
        channel->set_buffer_size(bufferSize);
    }
}

void Project::setup_default_hardware_buses()
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

void Project::private_add_sheet(Sheet * sheet)
{
    PENTER;
    m_RtSheets.append(sheet);
}

void Project::private_remove_sheet(Sheet * sheet)
{
    PENTER;
    m_RtSheets.remove(sheet);
}

void Project::sheet_removed(Sheet *sheet)

{
    m_sheets.removeAll(sheet);

    if (m_sheets.size() > 0) {
        set_current_session(m_sheets.last()->get_id());
    }

    emit sheetRemoved(sheet);
}

void Project::sheet_added(Sheet *sheet)
{
    m_sheets.append(sheet);
    emit sheetAdded(sheet);
}

QString Project::get_import_dir() const
{
    QDir dir;
    if (!dir.exists(m_importDir)) {
        return QDir::homePath();
    }

    return m_importDir;
}

void Project::set_import_dir(const QString& dir)
{
    m_importDir = dir;
}

bool Project::is_save_to_close() const
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

bool Project::is_recording() const
{
    foreach(Sheet* sheet, m_sheets) {
        if (sheet->is_recording() && sheet->is_transport_rolling()) {
            return true;
        }
    }
    return false;
}

void Project::set_work_at(TTimeRef worklocation, bool isFolder)
{
    foreach(Sheet* sheet, m_sheets) {
        sheet->set_work_at(worklocation, isFolder);
    }
}

void Project::set_sheets_are_tracks_folder(bool isFolder)
{
    m_sheetsAreTrackFolder = isFolder;
    if (m_sheetsAreTrackFolder) {
        info().information(tr("Sheets behave as Tracks Folder"));
    } else {
        info().information(tr("Sheets NO longer behave as Tracks Folder"));
    }
}


int Project::process( nframes_t nframes )
{
    int result = 0;

    for(Sheet* sheet = m_RtSheets.first(); sheet != nullptr; sheet = sheet->next) {
        result |= sheet->process(nframes);
    }

    TTimeRef startLocation = get_transport_location();
    TTimeRef endLocation = startLocation + TTimeRef(nframes, audiodevice().get_sample_rate());

    for(TBusTrack* busTrack = m_rtBusTracks.first(); busTrack != nullptr; busTrack = busTrack->next) {
        busTrack->process(startLocation, endLocation, nframes);
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
    m_masterOutBusTrack->process(startLocation, endLocation, nframes);

    return result;
}

int Project::transport_control(TTransportControl *transportControl)
{
    bool result = true;

    for(Sheet* sheet = m_RtSheets.first(); sheet != nullptr; sheet = sheet->next) {
        result = sheet->transport_control(transportControl);
    }

    return result;
}

TTimeRef Project::get_last_location() const
{
    TTimeRef lastLocation;

    foreach(Sheet* sheet, m_sheets) {
        TTimeRef location = sheet->get_last_location();
        if (location > lastLocation) {
            lastLocation = location;
        }
    }

    return lastLocation;
}

TTimeRef Project::get_transport_location() const
{
    if (!m_activeSheet) return TTimeRef();

    return m_activeSheet->get_transport_location();
}

QStringList Project::get_input_buses_for(TBusTrack *busTrack)
{
    QStringList buses;

    QList<AudioTrack*> audioTracks;
    foreach(Sheet* sheet, m_sheets) {
        audioTracks.append(sheet->get_audio_tracks());
    }

    foreach(AudioTrack* track, audioTracks) {
        // FIXME this is a temp fix!
        QList<TSend*> sends = track->get_post_sends();
        foreach(TSend* send, sends) {
            if (send->get_bus_id() == busTrack->get_id()) {
                buses.append(send->get_name());
            }
        }
    }

    return buses;
}

TCommand* Project::remove_child_session()
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
