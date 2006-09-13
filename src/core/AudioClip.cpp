/*
Copyright (C) 2005-2006 Remon Sijrier

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

$Id: AudioClip.cpp,v 1.43 2006/09/13 12:51:07 r_sijrier Exp $
*/

#include <cfloat>
#include <QInputDialog>

#include "ContextItem.h"
#include "AudioClip.h"
#include "AudioSource.h"
#include "ReadSource.h"
#include "WriteSource.h"
#include "ColorManager.h"
#include "Song.h"
#include "Track.h"
#include "AudioChannel.h"
#include "Mixer.h"
#include "DiskIO.h"
#include "Export.h"
#include "AudioClipManager.h"
#include "AudioSourceManager.h"
#include "Curve.h"
#include "FadeCurve.h"
#include "Tsar.h"
#include "ProjectManager.h"
#include "Peak.h"
#include "ContextPointer.h"
#include "Project.h"
#include "Utils.h"

#include <commands.h>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


AudioClip::AudioClip(const QString& name)
	: ContextItem(),
	  m_name(name)
{
	PENTERCONS;
	m_gain = m_normfactor = 1.0;
	m_length = sourceStartFrame = m_channels = sourceEndFrame = trackEndFrame = 0;
	isMuted=false;
	m_id = create_id();
	init();
}


AudioClip::AudioClip(const QDomNode& node)
	: ContextItem()
{
	PENTERCONS;
	QDomNode clipNode = node.firstChild();
	QDomElement e = node.toElement();
	m_id = e.attribute("id", "").toLongLong();
	m_readSourceId = e.attribute("source", "").toLongLong();
	m_domNode = node;
	init();
}

AudioClip::~AudioClip()
{
	PENTERDES;
	
	delete m_readSource;
}

void AudioClip::init()
{
	m_song = 0;
	m_track = 0;
	m_readSource = 0;
	isRecording = false;
	isSelected = false;
	fadeIn = 0;
	fadeOut = 0;
	m_channels = 0;
}

int AudioClip::set_state(const QDomNode& node)
{
	PENTER;
	
	QDomElement e = node.toElement();

	m_name = e.attribute( "clipname", "" ) ;
	PWARN("clipname is %s", QS_C(m_name));
	isTake = e.attribute( "take", "").toInt();
	m_channels = e.attribute( "channels", "0").toInt();
	set_gain( e.attribute( "gain", "" ).toFloat() );
	m_normfactor =  e.attribute( "normfactor", "1.0" ).toFloat();
	isMuted =  e.attribute( "mute", "" ).toInt();

	isSelected = e.attribute("selected", "0").toInt(); 

	if ( ! m_readSource ) {
		PWARN("no audio source set!");
		m_channels = 0;
		return -1;
	} else {
		set_audio_source(m_readSource);
	}

	sourceStartFrame = e.attribute( "sourcestart", "" ).toUInt();
	m_length = e.attribute( "length", "0" ).toUInt();
	sourceEndFrame = sourceStartFrame + m_length;
	set_track_start_frame( e.attribute( "trackstart", "" ).toUInt());

	QDomElement curvesNode = node.firstChildElement("Curves");
	if (!curvesNode.isNull()) {
		QDomElement fadeInNode = curvesNode.firstChildElement("FadeIn");
		if (!fadeInNode.isNull()) {
			fadeIn = new FadeCurve(this, "FadeIn");
			fadeIn->set_state( fadeInNode );
			emit fadeAdded(fadeIn);
			private_add_fade(fadeIn);
		}

		QDomElement fadeOutNode = curvesNode.firstChildElement("FadeOut");
		if (!fadeOutNode.isNull()) {
			fadeOut = new FadeCurve(this, "FadeOut");
			fadeOut->set_state( fadeOutNode );
			emit fadeAdded(fadeOut);
			private_add_fade(fadeOut);
		}
	}

	return 1;
}

QDomNode AudioClip::get_state( QDomDocument doc )
{
	QDomElement node = doc.createElement("Clip");
	node.setAttribute("trackstart", trackStartFrame);
	node.setAttribute("sourcestart", sourceStartFrame);
	node.setAttribute("length", m_length);
	node.setAttribute("gain", m_gain);
	node.setAttribute("normfactor", m_normfactor);
	node.setAttribute("mute", isMuted);
	node.setAttribute("take", isTake);
	node.setAttribute("channels", m_channels);
	node.setAttribute("clipname", m_name );
	node.setAttribute("selected", isSelected );
	node.setAttribute("id", m_id );

	node.setAttribute("source", m_readSource->get_id());

	QDomNode curves = doc.createElement("Curves");

	if (fadeIn)
		curves.appendChild(fadeIn->get_state(doc));
	if (fadeOut)
		curves.appendChild(fadeOut->get_state(doc));

	node.appendChild(curves);

	return node;
}

void AudioClip::toggle_mute()
{
	PENTER;
	isMuted=!isMuted;
	set_sources_active_state();
	emit muteChanged(isMuted);
}

void AudioClip::track_audible_state_changed()
{
	set_sources_active_state();
}

void AudioClip::set_sources_active_state()
{
	Q_ASSERT(m_track);
	
	if (! m_readSource) {
		return;
	}
	
	if ( m_track->is_muted() || m_track->is_muted_by_solo() || is_muted() ) {
			m_readSource->set_inactive();
	} else {
			m_readSource->set_active();
	}

}

void AudioClip::set_left_edge(nframes_t newFrame)
{

	if (newFrame < trackStartFrame) {

		int availableFramesLeft = sourceStartFrame;

		int movingToLeft = trackStartFrame - newFrame;

		if (movingToLeft > availableFramesLeft) {
			movingToLeft = availableFramesLeft;
		}

		trackStartFrame -= movingToLeft;
		set_source_start_frame( sourceStartFrame - movingToLeft );

	} else if (newFrame > trackStartFrame) {

		int availableFramesRight = m_length;

		int movingToRight = newFrame - trackStartFrame;

		if (movingToRight > availableFramesRight) {
			movingToRight = availableFramesRight;
		}

		trackStartFrame += movingToRight;
		set_source_start_frame( sourceStartFrame + movingToRight );

	} else {
		return;
	}

	emit positionChanged();
}

void AudioClip::set_right_edge(nframes_t newFrame)
{
	PENTER;
	if (newFrame > trackEndFrame) {

		int availableFramesRight = sourceLength - sourceEndFrame;

		int movingToRight = newFrame - trackEndFrame;

		if (movingToRight > availableFramesRight) {
			movingToRight = availableFramesRight;
		}

		set_track_end_frame( trackEndFrame + movingToRight );
		set_source_end_frame( sourceEndFrame + movingToRight );

	} else if (newFrame < trackEndFrame) {

		int availableFramesLeft = m_length;

		int movingToLeft = trackEndFrame - newFrame;

		if (movingToLeft > availableFramesLeft) {
			movingToLeft = availableFramesLeft;
		}

		set_track_end_frame( trackEndFrame - movingToLeft );
		set_source_end_frame( sourceEndFrame - movingToLeft);

	} else {
		return;
	}

	emit positionChanged();
}

void AudioClip::set_source_start_frame(nframes_t frame)
{
	sourceStartFrame = frame;
	m_length = sourceEndFrame - sourceStartFrame;
}

void AudioClip::set_source_end_frame(nframes_t frame)
{
	sourceEndFrame = frame;
	m_length = sourceEndFrame - sourceStartFrame;
}

void AudioClip::set_track_start_frame(nframes_t newTrackStartFrame)
{
	trackStartFrame = newTrackStartFrame;

	set_track_end_frame(trackStartFrame + m_length);

	emit positionChanged();
}

void AudioClip::set_track_end_frame( nframes_t endFrame )
{
// 	PWARN("trackEndFrame is %d", endFrame);
	trackEndFrame = endFrame;
	emit trackEndFrameChanged();
}

void AudioClip::set_blur(bool )
{
	emit stateChanged();
}

void AudioClip::set_fade_in(nframes_t b)
{
	get_fade_in()->set_range( b );
	emit stateChanged();
}

void AudioClip::set_fade_out(nframes_t b)
{
	Q_ASSERT(fadeOut);
	get_fade_out()->set_range( b );
	emit stateChanged();
}

void AudioClip::set_gain(float gain)
{
	PENTER3;
	if (gain < 0.0)
		gain = 0.0;
	if (gain > 32.0)
		gain = 32.0;
	m_gain = gain;
	emit gainChanged();
}

int AudioClip::set_selected(bool selected)
{
	if (isSelected != selected) {
		isSelected = selected;
		emit stateChanged();
	}
	return 1;
}

//
//  Function called in RealTime AudioThread processing path
//
int AudioClip::process(nframes_t nframes, audio_sample_t* mixdown, uint channel)
{
	Q_ASSERT(m_song);
	
	if (isRecording) {
		process_capture(nframes, channel);
		return 0;
	}


	if (channel >= m_channels) {
		return -1;	// Channel doesn't exist!!
	}

	bool showdebug = false;

	if ( ((m_song->get_transport_frame() < (548864 + 3000))) && (channel == 0)) {
// 		showdebug = true;
	}

	if (showdebug) {
		printf("%s\n", m_name.toAscii().data());
		printf("trackStartFrame is %d\n", trackStartFrame);
		printf("trackEndFrame is %d\n", trackEndFrame);
		printf("song->transport is %d\n", m_song->get_transport_frame());
		printf("diff trackEndFrame, song->transport is %d\n", (int)trackEndFrame - m_song->get_transport_frame());
		printf("diff trackStartFrame, song->transport is %d\n", (int)trackStartFrame - m_song->get_transport_frame());
	}

	nframes_t mix_pos;

	if (showdebug) printf("clip trackEndFrame - song->transport_frame is %d\n", trackEndFrame - m_song->get_transport_frame());

	if ( (trackStartFrame <= (m_song->get_transport_frame())) && (trackEndFrame > (m_song->get_transport_frame())) ) {
		mix_pos = m_song->get_transport_frame() - trackStartFrame + sourceStartFrame;
		if (showdebug) {
			printf("mix_pos is %d\n", mix_pos);
		}
	} else {
		if (showdebug) {
			printf("Not processing this Clip\n\n");
			printf("END %s\n\n", m_name.toAscii().data());
		}

		return 0;
	}


	if (isMuted || ( (m_gain * m_normfactor) == 0.0f) ) {
		return 0;
	}


	nframes_t read_frames = 0;


	if (m_song->realtime_path()) {
		read_frames = m_readSource->rb_read(channel, mixdown, mix_pos, nframes);
	} else {
		read_frames = m_readSource->file_read(channel, mixdown, mix_pos, nframes);
	}

	if (showdebug) {
		printf("read frames is %d\n", read_frames);
		printf("END %s\n\n", m_name.toAscii().data());
	}


	if (read_frames == 0) {
		return 0;
	}


	foreach(FadeCurve* fade, m_fades) {
		fade->process(mixdown, read_frames);
	}

	return 1;
}

//
//  Function called in RealTime AudioThread processing path
//
void AudioClip::process_capture( nframes_t nframes, uint channel )
{
	WriteSource* source = writeSources.at(channel);

	nframes_t written = source->rb_write(captureBus->get_buffer(channel, nframes), m_song->get_transport_frame(), nframes);

	if (written != nframes) {
		PWARN("couldn't write nframes to buffer, only %d", written);
	}
}

int AudioClip::init_recording( QByteArray name )
{
	Q_ASSERT(m_song);
	Q_ASSERT(m_track);
	
	captureBus = audiodevice().get_capture_bus(name);

	if (!captureBus) {
		info().warning( tr("Unable to Record to Track") );
		info().information( tr("AudioDevice doesn't have this Capture Bus: %1 (Track %2)").
				arg(name.data()).arg(m_track->get_id()) );
		return -1;
	}


	for (int chan=0; chan<captureBus->get_channel_count(); chan++) {
		ExportSpecification*  spec = new ExportSpecification;

		spec->exportdir = pm().get_project()->get_root_dir() + "/audiosources";
		spec->format = SF_FORMAT_WAV;
		spec->data_width = 1;	// 1 means float
		spec->format |= SF_FORMAT_FLOAT;
		spec->channels = 1;
		spec->sample_rate = audiodevice().get_sample_rate();
		spec->src_quality = SRC_SINC_MEDIUM_QUALITY;

		QString songid = QString::number(m_song->get_id())  + "_";
		if (m_song->get_id() < 10)
			songid.prepend("0");
		songid.prepend( "Song" );

		spec->name = songid + m_name + "-" + QByteArray::number(chan) + ".wav";

		spec->dataF = captureBus->get_buffer( chan, audiodevice().get_buffer_size());

		WriteSource* ws = new WriteSource(spec, 0);
		ws->set_process_peaks( true );
		ws->set_recording( true );

		connect(ws, SIGNAL(exportFinished( WriteSource* )), this, SLOT(finish_write_source( WriteSource* )));

		writeSources.insert(chan, ws);
		m_song->get_diskio()->register_write_source( ws );
	}

	sourceStartFrame = 0;
	isTake = 1;
	m_channels = captureBus->get_channel_count();
	isRecording = true;
	connect(m_song, SIGNAL(transferStopped()), this, SLOT(finish_recording()));

	return 1;
}

Command* AudioClip::remove_from_selection()
{
	return new ClipSelection(this, "remove_from_selection", tr("Selection: Remove Clip"));
}

Command * AudioClip::add_to_selection()
{
	return new ClipSelection(this, "add_to_selection", tr ("Selection: Add Clip"));
}

Command* AudioClip::select()
{
	return new ClipSelection(this, "select_clip", tr("Select Clip"));
}

Command* AudioClip::mute()
{
	return new PCommand(this, "toggle_mute", tr("Toggle Mute"));
}

Command* AudioClip::reset_gain()
{
	return new Gain(this, 1.0);
}

Command* AudioClip::reset_fade_in()
{
	set_fade_in(1);
	return (Command*) 0;
}

Command* AudioClip::reset_fade_out()
{
	set_fade_out(1);
	return (Command*) 0;
}

Command* AudioClip::reset_fade_both()
{
	set_fade_in(1);
	set_fade_out(1);
	return (Command*) 0;
}

Command* AudioClip::drag()
{
	Q_ASSERT(m_song);
	return new MoveClip(m_song, this);
}

Command* AudioClip::drag_edge()
{
	Q_ASSERT(m_song);
	int x = cpointer().clip_area_x();
	int cxm = m_song->frame_to_xpos( trackStartFrame + ( m_length / 2 ) );

	MoveEdge* me;

	if (x < cxm)
		me =   new  MoveEdge(this, "set_left_edge");
	else
		me = new MoveEdge(this, "set_right_edge");

	return me;
}

Command* AudioClip::gain()
{
	return new Gain(this);
}

Command* AudioClip::split()
{
	Q_ASSERT(m_song);
	return new SplitClip(m_song, this);
}

Command* AudioClip::copy()
{
	Q_ASSERT(m_song);
	return new CopyClip(m_song, this);
}

AudioClip * AudioClip::prev_clip( )
{
	Q_ASSERT(m_track);
	return m_track->get_cliplist().prev(this);
}

AudioClip * AudioClip::next_clip( )
{
	Q_ASSERT(m_track);
	return m_track->get_cliplist().next(this);
}

AudioClip* AudioClip::create_copy( )
{
	Q_ASSERT(m_track);
	QDomDocument doc("AudioClip");
	QDomNode clipState = get_state(doc);
	AudioClip* clip = new AudioClip(clipState);
	clip->set_name( clip->get_name().prepend(tr("Copy of - ")) );
	return clip;
}

Peak* AudioClip::get_peak_for_channel( int chan ) const
{
	PENTER2;
	Q_ASSERT(m_readSource);
	return m_readSource->get_peak(chan);
}

void AudioClip::set_audio_source(ReadSource* rs)
{
	PENTER;
	Q_ASSERT(m_song);
	
	m_readSource = rs;
	sourceLength = rs->get_nframes();

	// If m_length isn't set yet, it means we are importing stuff instead of reloading from project file.
	// it's a bit weak this way, hopefull I'll get up something better in the future.
	// The positioning-length-offset and such stuff is still a bit weak :(
	if (m_length == 0) {
		sourceEndFrame = rs->get_nframes();
		m_length = sourceEndFrame;
	}

	set_track_end_frame( trackStartFrame + sourceLength - sourceStartFrame);
	m_song->get_diskio()->register_read_source( rs );
	m_channels = rs->get_channel_count();

	set_sources_active_state();

	rs->set_audio_clip(this);

	emit stateChanged();
}

void AudioClip::finish_write_source( WriteSource * ws )
{
	PENTER;

//  	printf("AudioClip::finish_write_source :  thread id is: %ld\n", QThread::currentThreadId ());

	for (int i=0; i<writeSources.size(); ++i) {
		if (ws == writeSources.at(i)) {

			//FIXME. The audiosourcesmanager is actually in charge of creating
			// new ReadSources!!!!!!
			// So we should move this code into asm, and get the source from there again :-)
			// As a temp. fix, we call rs->ref() here to not crash :-(
/*			AudioSourceManager* asmanager =pm().get_project()->get_audiosource_manager();
			QString dir = ws->get_dir();
			QString name = ws->get_name();
			int channel = ws->get_channel();
			
			delete ws;
			
			ReadSource* rs = asmanager->new_readsource(dir,
								   name, 
								   channel, 
								   m_song->get_id(), 
								   audiodevice().get_bit_depth(), 
								   audiodevice().get_sample_rate() );

			set_audio_source(rs);

			ws = 0;*/
		}
	}
}

void AudioClip::finish_recording()
{
	PENTER;

	foreach(WriteSource* ws, writeSources) {
		ws->set_recording(false);
	}

	disconnect(m_song, SIGNAL(transferStopped()), this, SLOT(finish_recording()));

	isRecording = false;
}

int AudioClip::get_channels( ) const
{
	return m_channels;
}

Song* AudioClip::get_song( ) const
{
	Q_ASSERT(m_song);
	return m_song;
}

Track* AudioClip::get_track( ) const
{
	Q_ASSERT(m_track);
	return m_track;
}

void AudioClip::set_song( Song * song )
{
	m_song = song;
	set_history_stack(m_song->get_history_stack());
	m_song->get_audioclip_manager()->add_clip( this );
	
	if (isSelected) {
		m_song->get_audioclip_manager()->add_to_selection( this );
	}
}


void AudioClip::set_track( Track * track )
{
	if (m_track) {
		disconnect(m_track, SIGNAL(audibleStateChanged()), this, SLOT(track_audible_state_changed()));
	}
	
	m_track = track;
	
	connect(m_track, SIGNAL(audibleStateChanged()), this, SLOT(track_audible_state_changed()));
	set_sources_active_state();
}

void AudioClip::set_name( const QString& name )
{
	m_name = name;
}

float AudioClip::get_gain( ) const
{
	return m_gain;
}

float AudioClip::get_norm_factor( ) const
{
	return m_normfactor;
}

bool AudioClip::is_selected( ) const
{
	return isSelected;
}

bool AudioClip::is_take( ) const
{
	return isTake;
}

bool AudioClip::is_muted( ) const
{
	return isMuted;
}

QString AudioClip::get_name( ) const
{
	return m_name;
}

int AudioClip::get_bitdepth( ) const
{
	Q_ASSERT(m_readSource);
	return m_readSource->get_bit_depth();
}

int AudioClip::get_rate( ) const
{
	Q_ASSERT(m_readSource);
	return m_readSource->get_rate();
}

nframes_t AudioClip::get_source_length( ) const
{
	return sourceLength;
}

nframes_t AudioClip::get_length() const
{
	return m_length;
}

int AudioClip::get_baseY() const
{
	Q_ASSERT(m_track);
	return m_track->real_baseY() + 3;
}

int AudioClip::get_width() const
{
	nframes_t nframes = sourceEndFrame - sourceStartFrame;
	int xwidth = (int) ( nframes / Peak::zoomStep[m_song->get_hzoom()] );
	return xwidth;
}

int AudioClip::get_height() const
{
	Q_ASSERT(m_track);
	return m_track->get_height() - 8;
}

bool AudioClip::is_recording( ) const
{
	return isRecording;
}

nframes_t AudioClip::get_source_end_frame( ) const
{
	return sourceEndFrame;
}

nframes_t AudioClip::get_source_start_frame( ) const
{
	return sourceStartFrame;
}

nframes_t AudioClip::get_track_end_frame( ) const
{
	return trackEndFrame;
}

nframes_t AudioClip::get_track_start_frame( ) const
{
	return trackStartFrame;
}


Command * AudioClip::clip_fade_in( )
{
	int direction = 1;
	return new Fade(this, get_fade_in(), direction);
}

Command * AudioClip::clip_fade_out( )
{
	int direction = -1;
	return new Fade(this, get_fade_out(), direction);
}

Command * AudioClip::normalize( )
{
        bool ok;
        double d = QInputDialog::getDouble(0, tr("Normalization"),
                                           tr("Set Normalization level:"), 0.0, -120, 0, 1, &ok, Qt::Tool);
        if (ok)
		calculate_normalization_factor(d);

	// Hmm, this is not entirely true, but "almost" ;-)
	emit gainChanged();

	return (Command*) 0;
}

Command * AudioClip::denormalize( )
{
	m_normfactor = 1.0;
	// Hmm, this is not entirely true, but "almost" ;-)
	emit gainChanged();

	return (Command*) 0;
}

void AudioClip::calculate_normalization_factor(float targetdB)
{
	double maxamp = 0;

	float target = dB_to_scale_factor (targetdB);

	if (target == 1.0f) {
		/* do not normalize to precisely 1.0 (0 dBFS), to avoid making it appear
		   that we may have clipped.
		*/
		target -= FLT_EPSILON;
	}

	for (uint i=0; i<m_channels; ++i) {
		maxamp = f_max(m_readSource->get_peak(i)->get_max_amplitude(sourceStartFrame, sourceEndFrame), maxamp);
	}

	if (maxamp == 0.0f) {
		/* don't even try */
		return;
	}

	if (maxamp == target) {
		/* we can't do anything useful */
		return;
	}

	/* compute scale factor */
	m_normfactor = target/maxamp;
}

FadeCurve * AudioClip::get_fade_in( )
{
	if (!fadeIn) {
		fadeIn = new FadeCurve(this, "FadeIn");
		fadeIn->set_shape("Linear");
		THREAD_SAVE_CALL_EMIT_SIGNAL(this, fadeIn, private_add_fade(FadeCurve*), fadeAdded(FadeCurve*));
	}

	return fadeIn;
}

FadeCurve * AudioClip::get_fade_out( )
{
	if (!fadeOut) {
		fadeOut = new FadeCurve(this, "FadeOut");
		fadeOut->set_shape("Linear");
		THREAD_SAVE_CALL_EMIT_SIGNAL(this, fadeOut, private_add_fade(FadeCurve*), fadeAdded(FadeCurve*));
	}

	return fadeOut;
}

void AudioClip::private_add_fade( FadeCurve* fade )
{
	m_fades.append(fade);
}

void AudioClip::private_remove_fade( FadeCurve * fade )
{
	m_fades.append(fade);
}

qint64 AudioClip::get_id( ) const
{
	return m_id;
}

qint64 AudioClip::get_readsource_id( )
{
	return m_readSourceId;
}

void AudioClip::set_clip_readsource( ReadSource * source )
{
	m_readSource = source;
}

// eof

