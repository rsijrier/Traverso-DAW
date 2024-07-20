/*
Copyright (C) 2005-2008 Remon Sijrier 

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

#ifndef AUDIOCLIP_H
#define AUDIOCLIP_H

#include <QString>
#include <QList>
#include <QDomNode>

#include "ContextItem.h"
#include "FadeCurve.h"
#include "TAudioProcessingNode.h"
#include "TLocation.h"
#include "defines.h"


class Sheet;
class ReadSource;
class WriteSource;
class AudioTrack;
class Peak;
class AudioBus;
class PluginChain;

class AudioClip : public TAudioProcessingNode
{
	Q_OBJECT

public:

	AudioClip(const QString& name);
	AudioClip(const QDomNode& node);
	~AudioClip();

	enum RecordingStatus {
		NO_RECORDING,
		RECORDING,
  		FINISHING_RECORDING
	};
	
	void set_audio_source(ReadSource* source);
    int init_recording();
    int process(const TTimeRef& startLocation, const TTimeRef& endLocation, nframes_t nframes);

    // Re-implemented from LocationItem::set_location_start
    // preferably we wouldn't have to re-implement this function
    // TODO: make every location dependent item not have to re-implement ?
    void set_location_start(const TTimeRef& location);
	void set_fade_in(double range);
	void set_fade_out(double range);
	void set_track(AudioTrack* track);
	void set_sheet(Sheet* sheet);

	void set_selected(bool selected);
	void set_as_moving(bool moving);
	int set_state( const QDomNode& node );

	AudioClip* create_copy();
	AudioTrack* get_track() const;
	Sheet* get_sheet() const;
	Peak* get_peak() const {return m_peak;}
	QDomNode get_state(QDomDocument doc);
	FadeCurve* get_fade_in() const;
	FadeCurve* get_fade_out() const;
	
    TTimeRef get_source_length() const;
    TTimeRef get_length() const {return m_length;}
    TTimeRef get_source_start_location() const {return m_sourceStartLocation;}
    TTimeRef get_source_end_location() const {return m_sourceEndLocation;}
	
    uint get_channel_count() const;
    uint get_rate() const;
    uint get_bitdepth() const;
	qint64 get_readsource_id() const;
	qint64 get_sheet_id() const {return m_sheetId;}
	ReadSource* get_readsource() const;
    inline TLocation* get_location() const {return m_locationItem;}
	
	QDomNode get_dom_node() const;
	
	bool is_take() const;
	bool is_selected();
	bool is_locked() const {return m_isLocked;}
	bool has_sheet() const;
    bool is_readsource_invalid() const {return !m_isReadSourceValid;}

    bool operator<(const AudioClip &other) {
        return this->get_location()->get_start() < other.get_location()->get_start();
    }    

    bool is_moving() const {return m_isMoving;}

	int recording_state() const;

    float calculate_normalization_factor(float targetdB = 0.0);

    void removed_from_track();

    AudioClip* next = nullptr;


private:
    Sheet*          m_sheet;
    AudioTrack* 	m_track;
    ReadSource*		m_readSource;
    WriteSource*	m_writer;
    TRealTimeLinkedList<FadeCurve*>	m_fades;
	Peak* 			m_peak;
    FadeCurve*		m_fadeIn;
    FadeCurve*		m_fadeOut;
	QDomNode		m_domNode;
    TLocation*   m_locationItem;
	
    TTimeRef 		m_sourceEndLocation;
    TTimeRef 		m_sourceStartLocation;
    TTimeRef			m_sourceLength;
    TTimeRef 		m_length;

	bool 			m_isTake;
	bool			m_isLocked;
	bool			m_isReadSourceValid;
	bool			m_isMoving;
    bool            m_syncDuringDrag;
    RecordingStatus m_recordingStatus;
	
	qint64			m_readSourceId;
	qint64			m_sheetId;

    void create_fade(FadeCurve::FadeType fadeType);
	void init();
    void set_source_end_location(const TTimeRef& location);
    void set_source_start_location(const TTimeRef& location);
    void set_track_end_location(const TTimeRef& location);
	void set_sources_active_state();
	void process_capture(nframes_t nframes);
		
	friend class ResourcesManager;

signals:
    void muteChanged();
    void lockChanged();
	void positionChanged();
	void fadeAdded(FadeCurve*);
	void fadeRemoved(FadeCurve*);
	void recordingFinished(AudioClip*);

public slots:
	void finish_recording();
	void finish_write_source();
    void set_left_edge(TTimeRef newLeftLocation);
    void set_right_edge(TTimeRef newRightLocation);
	void track_audible_state_changed();
	void toggle_mute();
	void toggle_lock();
	
	TCommand* mute();
	TCommand* reset_fade_in();
	TCommand* reset_fade_out();
	TCommand* reset_fade_both();
    TCommand* normalize();
	TCommand* lock();
    TCommand* toggle_show_gain_automation_curve();

private slots:
	void private_add_fade(FadeCurve* fade);
	void private_remove_fade(FadeCurve* fade);
    void update_global_configuration();
};


inline qint64 AudioClip::get_readsource_id( ) const {return m_readSourceId;}

#endif
