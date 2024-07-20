/*
Copyright (C) 2005-2010 Remon Sijrier

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

#ifndef AUDIO_TRACK_VIEW_H
#define AUDIO_TRACK_VIEW_H

#include "TrackView.h"

class AudioClip;
class AudioClipView;
class AudioTrack;
class AudioTrackPanelView;
class PluginChainView;

class AudioTrackView : public TrackView
{
	Q_OBJECT

public:
        AudioTrackView(SheetView* sv, AudioTrack* track);
        ~AudioTrackView() {}
	
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
	
        AudioTrack* get_track() const {return m_track;}
        AudioClipView* get_nearest_audioclip_view(TTimeRef location) const;
        QList<AudioClipView* > get_clipviews() {return m_clipViews;}
	CurveView* get_gain_curve_view() const {return m_curveView;}
	
        int get_height() const;
	
	void load_theme_data();
	
	void to_front(AudioClipView* view);
	
private:
        AudioTrack*		m_track;
	QList<AudioClipView* >	m_clipViews;

public slots:
	TCommand* insert_silence();
        TCommand* show_track_gain_curve();

protected slots:
	void automation_visibility_changed();

private slots:
	void add_new_audioclipview(AudioClip* clip);
	void remove_audioclipview(AudioClip* clip);
};


#endif // AUDIO_TRACK_VIEW

//eof
