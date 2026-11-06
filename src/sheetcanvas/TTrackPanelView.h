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

#pragma once

#include "TViewItem.h"

#include <functional>

class TTrack;
class TAudioTrack;
class TTrackView;
class TTrackLaneView;
class TTrackPanelView;
class TAudioTrackView;
class TTrackPanelViewPort;
class PanelLed;
class TAudioTrackPanelView;
class TBusTrackView;
class TVUMeterView;
class TPanKnobView;
class TGainKnobView;
class TTextView;

class TrackPanelGain : public TViewItem
{
	Q_OBJECT

public:
        TrackPanelGain(TTrackPanelView* parent, TTrack* track);
	TrackPanelGain(){}

	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
	void set_width(int width);
        void load_theme_data();

public slots:
	TCommand* gain_increment();
	TCommand* gain_decrement();
	
private:
        TTrack* m_track;
        QLinearGradient	m_gradient2D;
};


class TrackPanelLed : public TViewItem
{
	Q_OBJECT
public:
    // toggle runs when the button is pressed: a call the compiler checks,
    // not a slot found by its name
    TrackPanelLed(TTrackPanelView* view, const QString& name, std::function<TCommand*()> toggle);
	
	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
	void set_bounding_rect(QRectF rect);

private:
        QString m_name;
        std::function<TCommand*()> m_toggle;
        bool    m_isOn;

public slots:
        void ison_changed(bool isOn);
	
	TCommand* toggle();
};

class TTrackPanelView : public TViewItem
{
	Q_OBJECT

public:
        TTrackPanelView(TTrackView* trackView);
        ~TTrackPanelView();

	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
	void calculate_bounding_rect();
	
        TTrackView* get_track_view() const {return m_trackView;}

protected:
        TTrack*                  m_track;
        TTrackView*              m_trackView;
	TTrackPanelViewPort*	m_viewPort;
	
	TrackPanelLed*          m_infoLed;
	TrackPanelLed*          m_muteLed;
	TrackPanelLed*          m_soloLed;
	TrackPanelLed*          m_portLed{};
	TrackPanelLed*          m_preLedButton;
	TPanKnobView*	        m_panKnob;
    TGainKnobView*          m_gainKnob;
    TTextView*              m_trackNameView;
    int LED_WIDTH;
    int LED_HEIGHT;
    int LED_Y_POS;
    int VU_WIDTH;
    int GAIN_Y_POS{};
    int VUMETER_Y_POS;
    int INDENT;
    int PANEL_ITEM_SPACING;
    int LED_SPACING;


        TVUMeterView*            m_vuMeterView;

    QMap<int, TViewItem*>	m_ledViews;

        virtual void layout_panel_items();

private slots:
        void update_name();
        void theme_config_changed();
        void active_context_changed() {update();}
};



class TAudioTrackPanelView : public TTrackPanelView
{
        Q_OBJECT

public:
        TAudioTrackPanelView(TAudioTrackView* trackView);
        ~TAudioTrackPanelView();

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

protected:
        void layout_panel_items();


private:
        TAudioTrackView*	m_tv;
        TrackPanelLed*  m_recLed;
};


class TBusTrackPanelView : public TTrackPanelView
{
        Q_OBJECT

public:
        TBusTrackPanelView(TBusTrackView* trackView);
        ~TBusTrackPanelView();

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);


protected:
        void layout_panel_items();


private:
};



class TTrackLanePanelView : public TViewItem
{
	Q_OBJECT

public:
	TTrackLanePanelView(TTrackLaneView* laneView);
	~TTrackLanePanelView();

	void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

	void calculate_bounding_rect();

private:
	TTrackLaneView*	m_laneView;

};


//eof
 
