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

#pragma once

#include <QString>
#include <memory>
#include <QList>
#include <QDomNode>

#include "TObjectFactory.h"
#include "TSession.h"
#include "TRtList.h"
#include "TProcessContext.h"
#include "TAudioDeviceSetup.h"
#include "TRealTime.h"

class AudioBus;
class AudioChannel;
class TAudioBusConfiguration;
class TTransportControl;
class TSheet;
class TIOScheduler;
class TRtTransaction;
class TTrack;
class TResourcesManager;
class TPeakBuildService;
class TExportSpecification;
class TAudioDeviceClient;
class TBusTrack;
class TSend;
class TAudioPlugin;
class SpectralMeter;
class CorrelationMeter;

class TProject : public TSession
{
    Q_OBJECT

public :
    ~TProject();

    int process(TProcessContext &context) T_REALTIME_SAFE;
    // jackd only feature
    int transport_control(TTransportControl* transportControl) T_REALTIME_SAFE;

    AudioBus* get_playback_bus(const QString& name) const;
    AudioBus* get_capture_bus(const QString& name) const;

    AudioBus* get_audio_bus(qint64 id);
    AudioBus* create_software_audio_bus(const TAudioBusConfiguration& config);
    void remove_software_audio_bus(AudioBus* bus);
    QList<TSend*> get_inputs_for_bus_track(TBusTrack* busTrack) const;
    void setup_default_hardware_buses();

    QStringList get_playback_buses_names( ) const;
    QStringList get_capture_buses_names( ) const;

    QList<AudioBus*> get_hardware_buses() const;
    QList<TTrack*> get_sheet_tracks() const;
    TTrack* get_track(qint64 trackId) const;


    // Get functions
    qint64 get_current_sheet_id() const;
    int get_num_sheets() const;
    uint get_rate() const;
    // The project's rate: the user's wish, which recordings and exports are
    // written at. Until the project rate work (doc/status.md, decisions
    // 2026-10-28) it is the rate the device was granted, as before; that
    // work changes this function only.
    uint project_rate() const;
    uint get_bitdepth() const;
    TTimeRef get_last_location() const;
    TTimeRef get_transport_location() const T_REALTIME_SAFE;

    TResourcesManager* get_audiosource_manager() const;
    // Makes and loads the objects with an id, and finds them by it
    // (doc/design/object-ids.md)
    TObjectFactory& objects() { return m_objects; }
    TObjectPool& object_pool() { return m_objectPool; }
    TPeakBuildService* get_peak_builds() const { return m_peakBuilds.get(); }
    QString get_title() const;
    QString get_engineer() const;
    QString get_description() const;
    QString get_discid() const;
    QString get_performer() const;
    QString get_arranger() const;
    QString get_songwriter() const;
    QString get_message() const;
    QString get_upc_ean() const;
    int get_genre();
    QString get_root_dir() const;
    QString get_audiosources_dir() const;
    QString get_import_dir() const;
    QString get_error_string() const {return m_errorString;}
    QList<TSheet* > get_sheets() const;
    // Every sheet's undone recording sessions, before the project closes
    void discard_undone_recordings();
    TSession* get_session(qint64 id);
    QList<TSession*> get_sessions();
    TSession* get_current_session() const ;
    TSheet* get_active_sheet() const {return m_activeSheet;}
    TSheet* get_sheet(qint64 id) const;
    int get_sheet_index(qint64 id);
    int get_session_index(qint64 id);
    int get_keyboard_arrow_key_navigation_speed() const {return m_keyboardArrowNavigationSpeed;}
    QDomNode get_state(QDomDocument doc, bool istemplate=false);


    // Set functions
    void set_title(const QString& title);
    void set_engineer(const QString& pEngineer);
    void set_description(const QString& des);
    void set_discid(const QString& pId);
    void set_performer(const QString& pPerformer);
    void set_arranger(const QString& pArranger);
    void set_songwriter(const QString& sw);
    void set_message(const QString& pMessage);
    void set_upc_ean(const QString& pUPC);
    void set_genre(int pGenre);
    void set_sheet_export_progress(int pogress);
    void set_current_session(qint64 id);
    void set_import_dir(const QString& dir);
    void set_sheets_are_tracks_folder(bool isFolder);
    void set_work_at(const TTimeRef &worklocation, bool isFolder);
    void set_keyboard_arrow_key_navigation_speed(int speed) {m_keyboardArrowNavigationSpeed = speed;}
    int save_from_template_to_project_file(const QString& file, const QString& projectName);


    TCommand* add_sheet(TSheet* sheet, bool historable=true);
    TCommand* remove_sheet(TSheet* sheet, bool historable=true);

    bool has_changed();
    bool is_save_to_close() const;
    bool is_recording() const;
    bool sheets_are_track_folder() const {return m_sheetsAreTrackFolder;}

    int save(bool autosave=false);
    int load(const QString &projectfile = "");
    int export_project();
    TExportSpecification* get_export_specification();

    enum {
        SETTING_XML_CONTENT_FAILED = -1,
        PROJECT_FILE_COULD_NOT_BE_OPENED = -2,
        PROJECT_FILE_VERSION_MISMATCH = -3
    };

    void connect_to_audio_device();
    int disconnect_from_audio_device();

    void add_meter(TAudioPlugin* meter);


public slots:
    void track_property_changed();
    TCommand* remove_child_session();

private:
    TProject(TAudioDevice& device, const QString& title);


    // First: everything made below gets its id from them
    TObjectIdGenerator       m_idGenerator;
    TObjectPool              m_objectPool{this};
    TObjectFactory           m_objects{m_idGenerator, m_objectPool};
    TResourcesManager*       m_resourcesManager;
    std::unique_ptr<TPeakBuildService> m_peakBuilds;
    TRtList<TSheet>          m_sheets;
    TSheet*                  m_activeSheet;
    TSession*               m_activeSession;
    TAudioDeviceClient*     m_audiodeviceClient;
    // The application's (TProjectManager), woken once per cycle from here
    TIOScheduler*           m_readScheduler;
    TIOScheduler*           m_writeScheduler;
    SpectralMeter*          m_spectralMeter;
    CorrelationMeter*       m_correlationMeter;

    QList<AudioBus* >       m_hardwareAudioBuses;

    QHash<qint64, AudioBus* >       m_softwareAudioBuses;
    QHash<qint64, AudioChannel* >   m_softwareAudioChannels;

    TExportSpecification*   m_exportSpecification;



    QString 	m_rootDir;
    QString 	m_sourcesDir;
    QString 	engineer;
    QString		m_description;
    QString		m_importDir;
    QString		m_discid;
    int         m_genre{};
    QString		m_upcEan;
    QString		m_performer;
    QString		m_arranger;
    QString		m_songwriter;
    QString		m_message;
    QString		m_errorString;

    uint		m_rate;
    uint		m_bitDepth;
    int         m_keyboardArrowNavigationSpeed;
    bool		m_useResampling;
    bool        m_sheetsAreTrackFolder{};

    bool        m_projectClosed;



    qint64 		m_activeSheetId;
    qint64      m_activeSessionId;

    int create(int sheetcount, int numtracks);
    int create_audiosources_dir();

    // The setup the project saves and starts the audio device with: read
    // from the project file, the global defaults for what it does not have
    void read_audio_device_setup(const QDomDocument& doc);
    TAudioDeviceSetup m_audioDeviceSetup;

    void set_project_closed();
    void log_object_ids(int warnings, bool invariantHolds) const;

    friend class TProjectManager;
    
private:
    void private_add_sheet(TSheet* sheet, TRtTransaction& transaction);
    void private_remove_sheet(TSheet* sheet, TRtTransaction& transaction);

private slots:
    void audiodevice_params_changed();
    void export_finished();
    void audio_device_removed_client(TAudioDeviceClient*client);
    
signals:
    void currentSessionChanged(TSession* );
    void sessionIsAlreadyCurrent(TSession* );
    void sheetAdded(TSheet*);
    void sheetRemoved(TSheet*);
    void projectLoadFinished();
    void projectLoadStarted();
    void trackPropertyChanged();

    void exportFinished();
};

