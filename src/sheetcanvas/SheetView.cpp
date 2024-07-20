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
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-11  USA.

*/


#include <QScrollBar>
#include <QInputDialog>

#include "TConfig.h"
#include "Curve.h"
#include "TInputEventDispatcher.h"
#include "Sheet.h"
#include "AudioClip.h"
#include "SnapList.h"
#include "AudioTrack.h"
#include "Marker.h"
#include "TBusTrack.h"
#include "ContextPointer.h"
#include "Themer.h"
#include "AudioClipView.h"
#include "CurveView.h"
#include "CurveNodeView.h"
#include "MarkerView.h"
#include "SheetView.h"
#include "SheetWidget.h"
#include "AudioTrackView.h"
#include "TBusTrackView.h"
#include "TrackPanelView.h"
#include "Cursors.h"
#include "ClipsViewPort.h"
#include "TimeLineViewPort.h"
#include "TimeLineView.h"
#include "TrackPanelViewPort.h"
#include "TCanvasCursor.h"
#include "TSession.h"
#include "ViewPort.h"
#include "TMoveCommand.h"

#include "ProjectManager.h"
#include "Project.h"

#include <Debugger.h>

QHash<QString, QString> SheetView::m_cursorsDict;

SheetView::SheetView(SheetWidget* sheetwidget,
	ClipsViewPort* viewPort,
	TrackPanelViewPort* tpvp,
	TimeLineViewPort* tlvp,
	TSession* session)
	: ViewItem(nullptr, session)
{
	setZValue(1);

	m_session = session;
	m_clipsViewPort = viewPort;
	m_tpvp = tpvp;
	m_tlvp = tlvp;
	m_vScrollBar = sheetwidget->m_vScrollBar;
	m_hScrollBar = sheetwidget->m_hScrollBar;
	m_actOnPlayHead = true;
	m_viewportReady = false;
    m_sheetMasterOutView = nullptr;
    m_projectMasterOutView = nullptr;
    timeref_scalefactor = TTimeRef::UNIVERSAL_SAMPLE_RATE;
    m_hasMouseTracking = true;

	m_clipsViewPort->scene()->addItem(this);

	m_playCursor = new PlayHead(this, m_session, m_clipsViewPort);
	m_workCursor = new WorkCursor(this, m_session);
    m_canvasCursor = new TCanvasCursor(this);
    scene()->addItem(m_canvasCursor);

    m_canvasCursorMoveAnimation = new QPropertyAnimation(m_canvasCursor, "position");
    m_canvasCursorMoveAnimation->setEasingCurve(QEasingCurve::InOutQuad);

	Sheet* sheet = qobject_cast<Sheet*>(m_session);

	if (m_session->is_project_session()) {
		m_projectMasterOutView = new TBusTrackView(this, pm().get_project()->get_master_out_bus_track());
	}
	if (sheet) {
		m_sheetMasterOutView = new TBusTrackView(this, m_session->get_master_out_bus_track());
	}

	connect(m_session, SIGNAL(workingPosChanged()), m_workCursor, SLOT(update_position()));
	connect(m_session, SIGNAL(transportStarted()), this, SLOT(follow_play_head()));
	connect(m_session, SIGNAL(transportLocationChanged()), this, SLOT(transport_position_set()));
	connect(m_session, SIGNAL(workingPosChanged()), this, SLOT(stop_follow_play_head()));
	connect(m_session, SIGNAL(verticalScrollBarValueChanged()), this, SLOT(session_vertical_scrollbar_position_changed()));
	connect(m_session, SIGNAL(horizontalScrollBarValueChanged()), this, SLOT(session_horizontal_scrollbar_position_changed()));


	m_clipsViewPort->scene()->addItem(m_playCursor);
	m_clipsViewPort->scene()->addItem(m_workCursor);

	m_clipsViewPort->setSceneRect(0, 0, MAX_CANVAS_WIDTH, MAX_CANVAS_HEIGHT);
	m_tlvp->setSceneRect(0, -TIMELINE_HEIGHT, MAX_CANVAS_WIDTH, 0);
    m_tpvp->setSceneRect(-m_tpvp->width(), 0, 0, MAX_CANVAS_HEIGHT);

	// Set up the viewports scale factor, and our timeref_scalefactor / m_peakCacheZoomFactor
	// Needed for our childs TrackView, AudioClipView, TimeLines MarkerViews etc which are created below.
	scale_factor_changed();

	connect(m_session, SIGNAL(hzoomChanged()), this, SLOT(scale_factor_changed()));
	connect(m_session, SIGNAL(tempFollowChanged(bool)), this, SLOT(set_follow_state(bool)));
	connect(m_session, SIGNAL(trackAdded(Track*)), this, SLOT(add_new_track_view(Track*)));
	connect(m_session, SIGNAL(trackRemoved(Track*)), this, SLOT(remove_track_view(Track*)));
	connect(m_session, SIGNAL(lastFramePositionChanged()), this, SLOT(update_scrollbars()));
	connect(m_hScrollBar, SIGNAL(sliderMoved(int)), this, SLOT(stop_follow_play_head()));
	connect(m_hScrollBar, SIGNAL(actionTriggered(int)), this, SLOT(hscrollbar_action(int)));
	connect(m_hScrollBar, SIGNAL(valueChanged(int)), this, SLOT(hscrollbar_value_changed(int)));
	connect(m_vScrollBar, SIGNAL(valueChanged(int)), m_clipsViewPort->verticalScrollBar(), SLOT(setValue(int)));

	connect(&cpointer(), SIGNAL(contextChanged()), this, SLOT(context_changed()));

	// fill the view with trackviews, add_new_trackview()
	// doesn't yet layout the new tracks.
	foreach(Track* track, m_session->get_tracks()) {
		add_new_track_view(track);
	}

	// this will call layout_tracks() for us too
	// which will continue now, due m_viewportReady is true now
    SheetView::load_theme_data();

	// Everything is in place to scroll to the last position
	// we were at, at closing this view.
	QPoint p = m_session->get_scrollbar_xy();
	m_session->set_scrollbar_x(p.x());
	m_session->set_scrollbar_y(p.y());
}

SheetView::~SheetView()
{
}

void SheetView::scale_factor_changed( )
{
	qreal zoom = m_session->get_hzoom();
    if (qFuzzyCompare(zoom, 0.0)) {
//		PERROR("Session %s return 0 hzoom factor!", m_session->get_name().toLatin1().data());
		// Woopsy, zoom can't be zero, if we allow that, timeref_scalefactor
		// will be zero too, and we use timeref_scalefactor as a divider so...:
		zoom = config().get_property("Sheet", "hzoomLevel", 8192).toInt();
	}
    timeref_scalefactor = qint64(zoom * (TTimeRef::UNIVERSAL_SAMPLE_RATE / 44100));
	m_tlvp->scale_factor_changed();

	update_tracks_bounding_rect();
}

AudioTrackView* SheetView::get_audio_trackview_at_scene_pos( QPointF point )
{
    AudioTrackView* view = nullptr;
	QList<QGraphicsItem*> views = m_clipsViewPort->items(m_clipsViewPort->mapFromScene(point));

	for (int i=0; i<views.size(); ++i) {
        view = dynamic_cast<AudioTrackView*>(views.at(i));
		if (view) {
			return view;
		}
	}
    return  nullptr;

}

TrackView* SheetView::get_trackview_at_scene_pos( QPointF point )
{
	QList<QGraphicsItem*> views = m_clipsViewPort->items(m_clipsViewPort->mapFromScene(point));

	for (int i=0; i<views.size(); ++i) {
		TrackView* view = dynamic_cast<TrackView*>(views.at(i));
		if (view) {
			return view;
		}
		TrackPanelView* tpv = dynamic_cast<TrackPanelView*>(views.at(i));
		if (tpv)
		{
			return tpv->get_track_view();
		}
	}
    return  nullptr;

}


void SheetView::move_trackview_up(TrackView *trackView)
{
	int index = trackView->get_track()->get_sort_index();
	if (index == 0 || trackView->get_track() == m_session->get_master_out_bus_track()) {
		// can't move any further up
		return;
	}

	AudioTrackView* atv = qobject_cast<AudioTrackView*>(trackView);
	TBusTrackView* btv = qobject_cast<TBusTrackView*>(trackView);

	int newindex = index - 1;

	if (atv) {
		for(int i=0; i<m_audioTrackViews.size(); i++) {
            if (i==newindex) {
                m_audioTrackViews.at(i)->get_track()->set_sort_index(i+1);
			}
		}
	}

	if (btv) {
		for(int i=0; i<m_busTrackViews.size(); i++) {
			if (i==newindex) {
				m_busTrackViews.at(i)->get_track()->set_sort_index(i+1);
			}
		}
	}


	trackView->get_track()->set_sort_index(newindex);

	layout_tracks();
}

void SheetView::move_trackview_down(TrackView *trackView)
{
	int index = trackView->get_track()->get_sort_index();
	if (index >= m_audioTrackViews.size() || trackView->get_track() == m_session->get_master_out_bus_track()) {
		// can't move any further down
		return;
	}

	AudioTrackView* atv = qobject_cast<AudioTrackView*>(trackView);
	TBusTrackView* btv = qobject_cast<TBusTrackView*>(trackView);

	int newindex = index + 1;

	if (atv) {
		for(int i=0; i<m_audioTrackViews.size(); i++) {
			if (i==newindex) {
				m_audioTrackViews.at(i)->get_track()->set_sort_index(i-1);
			}
		}
		if (newindex >= m_audioTrackViews.size()) {
			newindex = m_audioTrackViews.size() - 1;
		}
	}

	if (btv) {
		for(int i=0; i<m_busTrackViews.size(); i++) {
			if (i==newindex) {
				m_busTrackViews.at(i)->get_track()->set_sort_index(i-1);
			}
		}
		if (newindex >= m_busTrackViews.size()) {
			newindex = m_busTrackViews.size() - 1;
		}
	}


	trackView->get_track()->set_sort_index(newindex);

	layout_tracks();

}

void SheetView::to_bottom(TrackView *trackView)
{
	AudioTrackView* atv = qobject_cast<AudioTrackView*>(trackView);
	TBusTrackView* btv = qobject_cast<TBusTrackView*>(trackView);

	if (atv) {
		QList<TrackView*> list = m_audioTrackViews;
		list.removeAll(atv);
		for(int i=0; i<list.size(); i++) {
			list.at(i)->get_track()->set_sort_index(i);
		}
		atv->get_track()->set_sort_index(list.size());
	}

	if (btv) {
		QList<TrackView*> list = m_busTrackViews;
		list.removeAll(atv);

		for(int i=0; i<list.size(); i++) {
			list.at(i)->get_track()->set_sort_index(i);
		}
		btv->get_track()->set_sort_index(list.size());
	}


	layout_tracks();
}

void SheetView::to_top(TrackView *trackView)
{
	int index = trackView->get_track()->get_sort_index();
	if (index == 0) {
		// it's allready topmost, don't do anything
		return;
	}

	AudioTrackView* atv = qobject_cast<AudioTrackView*>(trackView);
	TBusTrackView* btv = qobject_cast<TBusTrackView*>(trackView);

	if (atv) {
		QList<TrackView*> list = m_audioTrackViews;
		list.removeAll(atv);
		atv->get_track()->set_sort_index(0);

		for(int i=0; i<list.size(); i++) {
			list.at(i)->get_track()->set_sort_index(i + 1);
		}
	}

	if (btv) {
		QList<TrackView*> list = m_busTrackViews;
		list.removeAll(atv);
		btv->get_track()->set_sort_index(0);

		for(int i=0; i<list.size(); i++) {
			list.at(i)->get_track()->set_sort_index(i + 1);
		}
	}


	layout_tracks();
}

void SheetView::add_new_track_view(Track* track)
{
    TrackView* view = nullptr;

	AudioTrack* audioTrack = qobject_cast<AudioTrack*>(track);
	TBusTrack* busTrack = qobject_cast<TBusTrack*>(track);

	int sortIndex = track->get_sort_index();

	if (audioTrack) {
		view = new AudioTrackView(this, audioTrack);
		if(sortIndex < 0) {
			track->set_sort_index(m_audioTrackViews.size());
		}
		m_audioTrackViews.append(view);
	}

	if (busTrack) {
		view = new TBusTrackView(this, busTrack);
		if(sortIndex < 0) {
			track->set_sort_index(m_busTrackViews.size());
		}
		m_busTrackViews.append(view);
	}

    if (view) {
        connect(view, SIGNAL(totalTrackHeightChanged()), this, SLOT(layout_tracks()));
    }

	layout_tracks();
}

void SheetView::remove_track_view(Track* track)
{
	QList<TrackView*> views;
	views.append(m_audioTrackViews);
	views.append(m_busTrackViews);

	foreach(TrackView* view, views) {
		if (view->get_track() == track) {
			TrackPanelView* panel = view->get_panel_view();
			scene()->removeItem(panel);
			scene()->removeItem(view);
			m_audioTrackViews.removeAll(view);
			m_busTrackViews.removeAll(view);
            delete view;
            delete panel;
			break;
		}
	}

	layout_tracks();
}

void SheetView::update_scrollbars()
{
    int width = int(m_session->get_last_location() / timeref_scalefactor) - (m_clipsViewPort->width() / 4);
	if (width < m_clipsViewPort->width() / 4) {
		width = m_clipsViewPort->width() / 4;
	}

	m_hScrollBar->setRange(0, width);
	m_hScrollBar->setSingleStep(m_clipsViewPort->width() / 10);
	m_hScrollBar->setPageStep(m_clipsViewPort->width());

	m_vScrollBar->setRange(0, m_sceneHeight - m_clipsViewPort->height() / 2);
	m_vScrollBar->setSingleStep(m_clipsViewPort->height() / 10);
	m_vScrollBar->setPageStep(m_clipsViewPort->height());

    m_playCursor->set_bounding_rect(QRectF(0, 0, 4, m_vScrollBar->maximum() + m_clipsViewPort->height()));
	m_playCursor->update_position();
	m_workCursor->set_bounding_rect(QRectF(0, 0, 1, m_vScrollBar->maximum() + m_clipsViewPort->height()));
	m_workCursor->update_position();

	set_snap_range(m_hScrollBar->value());
}

void SheetView::hscrollbar_value_changed(int value)
{
	// This slot is called when the hscrollbar value changes,
	// which can be due shuttling or playhead scrolling the page.
	// In that very case, we do NOT set the hscrollbar value AGAIN
	// but in case of a non-shuttle command, we call ie().jog to give the
	// command the opportunity to update (Gain-cursor position for example)
	// itself for the changed viewport / mouse coordinates.
	// FIXME This is NOT a solution to set hold-cursors at the correct
	// position in the viewport when it's scrolled programatically !!!!!
    // FIXME 2: when moving a clip beyond the viewport it starts to scroll
    // the view and so this function is called. It could crash Traverso
    // in the bspTree of QGraphicsScene so it certainly is not good to have
    // this here. for now disable calling ied().jog(), so far no behavioral change
    // noticed.
	if (ied().is_holding()) {
        TMoveCommand* s = dynamic_cast<TMoveCommand*>(ied().get_holding_command());
		if (!s) {
//			ied().jog();
		}
	} else {
		m_clipsViewPort->horizontalScrollBar()->setValue(value);
	}

	set_snap_range(value);
}

void SheetView::clipviewport_resize_event()
{
	update_scrollbars();
}

void SheetView::vzoom(qreal factor)
{
	PENTER;
	for (int i=0; i<m_audioTrackViews.size(); ++i) {
		TrackView* view = m_audioTrackViews.at(i);
		Track* track = view->get_track();
		int height = get_track_height(track);
        height = int(height * factor);
		if (height > m_trackMaximumHeight) {
			height = m_trackMaximumHeight;
		} else if (height < m_trackMinimumHeight) {
			height = m_trackMinimumHeight;
		}
		m_session->set_track_height(track->get_id(), height);
	}

	update_tracks_bounding_rect();
}


TCommand* SheetView::toggle_expand_all_tracks(int height)
{
	if (height < 0) {
		if (m_meanTrackHeight > m_trackMinimumHeight) {
			foreach(TrackView* view, get_track_views()) {
				m_session->set_track_height(view->get_track()->get_id(), m_trackMinimumHeight);
			}
		} else {
			foreach(TrackView* view, get_track_views()) {
				m_session->set_track_height(view->get_track()->get_id(), Track::INITIAL_HEIGHT);
			}
		}
	} else {
		foreach(TrackView* view, get_track_views()) {
			m_session->set_track_height(view->get_track()->get_id(), height);
		}
	}

	update_tracks_bounding_rect();

    return nullptr;
}

void SheetView::set_track_height(TrackView *view, int newheight)
{
	if (newheight > m_trackMaximumHeight) {
		newheight = m_trackMaximumHeight;
	}

	if (newheight < m_trackMinimumHeight) {
		newheight = m_trackMinimumHeight;
	}

	m_session->set_track_height(view->get_track()->get_id(), newheight);

	view->calculate_bounding_rect();
	layout_tracks();

	center_in_view(view, Qt::AlignVCenter);
}

void SheetView::hzoom(qreal factor)
{
	PENTER;
	m_session->set_hzoom(m_session->get_hzoom() * factor);
	center();
}


void SheetView::layout_tracks()
{    
	int verticalposition = m_trackTopIndent;
	int totalTrackHeightPrimaryLanes = 0;

	QList<TrackView*> views = get_track_views();
    std::sort(views.begin(), views.end(), [&](TrackView* left, TrackView* right) {
        return left->get_track()->get_sort_index() < right->get_track()->get_sort_index();
    });

	for (int i=0; i<views.size(); ++i) {
		TrackView* view = views.at(i);
		view->move_to(0, verticalposition);
		verticalposition += view->get_total_height() + m_trackSeperatingHeight;
		totalTrackHeightPrimaryLanes += get_track_height(view->get_track());
	}

	m_sceneHeight = verticalposition;

	// + 1, one for sheet master!
    m_meanTrackHeight = qreal(totalTrackHeightPrimaryLanes) / (m_audioTrackViews.size() + m_busTrackViews.size() + 1);

	update_scrollbars();
}

void SheetView::update_tracks_bounding_rect()
{
	QList<TrackView*> views = get_track_views();

	for (int i=0; i<views.size(); ++i) {
		views.at(i)->calculate_bounding_rect();
	}

	layout_tracks();
}


TCommand* SheetView::center()
{
	PENTER2;
	TTimeRef centerX;
	if (m_session->is_transport_rolling() && m_actOnPlayHead) {
		centerX = m_session->get_transport_location();
	} else {
		centerX = m_session->get_work_location();
	}

	int x = qRound(centerX / timeref_scalefactor);
	set_hscrollbar_value(x - m_clipsViewPort->width() / 2);
    return nullptr;
}


void SheetView::transport_position_set()
{
	if (!m_session->is_transport_rolling()) {
		m_playCursor->update_position();
	}
}


void SheetView::stop_follow_play_head()
{
    m_session->set_temp_follow_state(false);
}


void SheetView::follow_play_head()
{
    m_session->set_temp_follow_state(true);
}


void SheetView::set_follow_state(bool state)
{
	if (state) {
		m_actOnPlayHead = true;
		m_playCursor->enable_follow();
		m_playCursor->update_position();
	} else {
		m_actOnPlayHead = false;
		m_playCursor->disable_follow();
	}
}

TCommand* SheetView::goto_begin()
{
	stop_follow_play_head();
	m_session->set_work_at(TTimeRef());
	center();
    return nullptr;
}


TCommand* SheetView::goto_end()
{
	stop_follow_play_head();
	TTimeRef lastlocation = m_session->get_last_location();
	m_session->set_work_at(lastlocation);
	center();
    return nullptr;
}


TrackPanelViewPort* SheetView::get_trackpanel_view_port( ) const
{
	return m_tpvp;
}

ClipsViewPort * SheetView::get_clips_viewport() const
{
	return m_clipsViewPort;
}

TimeLineViewPort* SheetView::get_timeline_viewport() const
{
	return m_tlvp;
}

TCommand * SheetView::touch( )
{
    ViewPort* viewPort = dynamic_cast<ViewPort*>(cpointer().get_viewport());

    if (!viewPort) {
        return ied().failure();
    }

    if (viewPort == m_tpvp) {
		return ied().did_not_implement();
	}

	int x;

    // FIXME
    // we already checked for !viewPort so next if(!viewPort) never holds true
    // so x=on_first_input_event_x() must have served a purpose in the past
    // The whole logic of placing cursors needs to be reviewed anyways :)
    if (!viewPort) {
		x = cpointer().on_first_input_event_x();
	} else {
        x = cpointer().mouse_viewport_x();
	}

    m_session->set_work_at(TTimeRef(qRound(viewPort->map_to_scene(QPoint(x, 0)).x()) * timeref_scalefactor));

    return nullptr;
}

TCommand * SheetView::touch_play_cursor( )
{
	if (cpointer().get_viewport() == m_tpvp) {
		return ied().did_not_implement();
	}
	int x;
	if (!cpointer().get_viewport()) {
		x = cpointer().on_first_input_event_x();
	} else {
        x = cpointer().mouse_viewport_x();
	}
	m_session->set_transport_location(TTimeRef(qRound(m_clipsViewPort->mapToScene(x, 0).x()) * timeref_scalefactor));

	return nullptr;
}

void SheetView::set_snap_range(int start)
{
// 	printf("SheetView::set_snap_range\n");
	m_session->get_snap_list()->set_range(TTimeRef(start * timeref_scalefactor),
				TTimeRef((start + m_clipsViewPort->viewport()->width()) * timeref_scalefactor),
				timeref_scalefactor);
}

TCommand* SheetView::scroll_up( )
{
	PENTER3;
	set_vscrollbar_value(m_clipsViewPort->verticalScrollBar()->value() - int(m_meanTrackHeight * 0.75));

    return nullptr;
}

TCommand* SheetView::scroll_down( )
{
	PENTER3;
	set_vscrollbar_value(m_clipsViewPort->verticalScrollBar()->value() + int(m_meanTrackHeight * 0.75));
    return nullptr;
}

TCommand* SheetView::scroll_right()
{
	PENTER3;
	stop_follow_play_head();
	set_hscrollbar_value(m_clipsViewPort->horizontalScrollBar()->value() + 50);
    return nullptr;
}


TCommand* SheetView::scroll_left()
{
	PENTER3;
	stop_follow_play_head();
	set_hscrollbar_value(m_clipsViewPort->horizontalScrollBar()->value() - 50);
    return nullptr;
}

int SheetView::hscrollbar_value() const
{
	return m_clipsViewPort->horizontalScrollBar()->value();
}

void SheetView::hscrollbar_action(int action)
{
	if (action == QAbstractSlider::SliderPageStepAdd || action == QAbstractSlider::SliderPageStepSub) {
		stop_follow_play_head();
	}
}

int SheetView::vscrollbar_value() const
{
	return m_clipsViewPort->verticalScrollBar()->value();
}

void SheetView::load_theme_data()
{
	m_trackSeperatingHeight = themer()->get_property("Sheet:track:seperatingheight", 0).toInt();
    m_trackMinimumHeight = 55;
    m_trackMaximumHeight = 1024;
	m_trackTopIndent = themer()->get_property("Sheet:track:topindent", 6).toInt();

	m_clipsViewPort->setBackgroundBrush(themer()->get_color("Sheet:background"));
	m_tpvp->setBackgroundBrush(themer()->get_brush("TrackPanel:background", QPoint(0, 0), QPoint(0, m_tpvp->height())));

	update_tracks_bounding_rect();
}

TCommand * SheetView::add_marker()
{
	return m_tlvp->get_timeline_view()->add_marker();
}

TCommand * SheetView::add_marker_at_playhead()
{
	return m_tlvp->get_timeline_view()->add_marker_at_playhead();
}

TCommand * SheetView::add_marker_at_work_cursor()
{
	return m_tlvp->get_timeline_view()->add_marker_at_work_cursor();
}

TCommand * SheetView::center_playhead( )
{
	TTimeRef centerX = m_session->get_transport_location();
	set_hscrollbar_value(int(centerX / timeref_scalefactor - m_clipsViewPort->width() / 2));

	follow_play_head();

    return nullptr;
}

void SheetView::set_hscrollbar_value(int value)
{
	m_session->set_scrollbar_x(value);
}

void SheetView::set_vscrollbar_value(int value)
{
	if (value > m_vScrollBar->maximum()) {
		value = m_vScrollBar->maximum();
	}
	if (value < 0) {
		value = 0;
	}
	m_session->set_scrollbar_y(value);
}

void SheetView::session_vertical_scrollbar_position_changed()
{
	QPoint p = m_session->get_scrollbar_xy();

	m_clipsViewPort->verticalScrollBar()->setValue(p.y());
	m_vScrollBar->setValue(p.y());
}

void SheetView::session_horizontal_scrollbar_position_changed()
{
	QPoint p = m_session->get_scrollbar_xy();

	m_clipsViewPort->horizontalScrollBar()->setValue(p.x());
	m_hScrollBar->setValue(p.x());
}

void SheetView::browse_to_track(Track *track)
{
	QList<TrackView*> views = get_track_views();
	if (m_sheetMasterOutView) {
		views.append(m_sheetMasterOutView);
	}
	if (m_projectMasterOutView) {
		views.append(m_projectMasterOutView);
	}

	foreach(TrackView* view, views) {
		if (view->get_track() == track) {
			QList<ContextItem*> list;
			list.append(view);
			list.append(this);

			cpointer().set_active_context_items_by_keyboard_input(list);

            keyboard_move_canvas_cursor_to_location(m_session->get_work_location(), view->scenePos().y() + view->boundingRect().height() / 2);

			return;
		}
	}
}

void SheetView::browse_to_audio_clip_view(AudioClipView* acv)
{
	QList<ContextItem*> activeList;

	activeList.append(acv);
	activeList.append(acv->get_audio_track_view());
	activeList.append(this);

    if (m_canvasCursor->get_pos().x() > acv->scenePos().x() && (m_canvasCursor->get_pos().x() < (acv->scenePos().x() + acv->boundingRect().width()))) {
        keyboard_move_canvas_cursor_to_location(TTimeRef(m_canvasCursor->get_pos().x() * timeref_scalefactor), acv->scenePos().y() + acv->boundingRect().height() / 2);
    } else {
        keyboard_move_canvas_cursor_to_location(TTimeRef((acv->scenePos().x() + acv->boundingRect().width() / 2) * timeref_scalefactor), acv->scenePos().y() + acv->boundingRect().height() / 2);
    }


	cpointer().set_active_context_items_by_keyboard_input(activeList);
}

void SheetView::browse_to_curve_view(CurveView *curveView)
{
	QList<ContextItem*> activeList;
	AudioClipView* acv = static_cast<AudioClipView*>(curveView->parentItem());
	activeList.append(curveView);
	activeList.append(acv);
	activeList.append(acv->get_audio_track_view());
	activeList.append(this);
	cpointer().set_active_context_items_by_keyboard_input(activeList);
}

void SheetView::browse_to_marker_view(MarkerView *markerView)
{
	if (!markerView) {
		return;
	}

	QList<ContextItem*> contexts = cpointer().get_active_context_items();
	MarkerView* view;
	foreach(ContextItem* item, contexts) {
		view = qobject_cast<MarkerView*>(item);
		if (view) {
			cpointer().remove_from_active_context_list(item);
			contexts.removeAll(item);
		}
	}

    keyboard_move_canvas_cursor_to_location(TTimeRef(markerView->get_marker()->get_when()), cpointer().scene_y());

	contexts.prepend(markerView);
	cpointer().set_active_context_items_by_keyboard_input(contexts);
}

void SheetView::browse_to_curve_node_view(CurveNodeView *nodeView)
{
    QList<ContextItem*> activeList;
	CurveView* curveView = nodeView->get_curve_view();
    curveView->update_softselected_node(nodeView->scenePos());

	AudioClipView* acv = static_cast<AudioClipView*>(curveView->parentItem());
	activeList.append(curveView);
	activeList.append(acv);
	activeList.append(acv->get_audio_track_view());
	activeList.append(this);

    keyboard_move_canvas_cursor_to_location(TTimeRef(nodeView->get_curve_node()->get_when()) + curveView->get_curve()->get_start_offset(),
			   nodeView->scenePos().y() + nodeView->boundingRect().height() / 2);

	cpointer().set_active_context_items_by_keyboard_input(activeList);

}

TCommand* SheetView::browse_to_time_line()
{
	QList<ContextItem*> items = cpointer().get_active_context_items();
	items.prepend(m_tlvp->get_timeline_view());

	cpointer().set_active_context_items_by_keyboard_input(items);

	return nullptr;
}

void SheetView::collect_item_browser_data(ItemBrowserData &data)
{
	QList<ContextItem*> list = cpointer().get_active_context_items();

	if (!list.empty()) {
		data.currentContext = list.first()->metaObject()->className();
	}

	foreach(ContextItem* obj, list) {
        if (!data.timeLineView) {
			data.timeLineView = qobject_cast<TimeLineView*>(obj);
		}
		if (!data.markerView) {
			data.markerView = qobject_cast<MarkerView*>(obj);
		}
		if (!data.tv) {
			data.tv = qobject_cast<TrackView*>(obj);
		}
		if (!data.atv) {
			data.atv = qobject_cast<AudioTrackView*>(obj);
		}
		if (!data.acv) {
			data.acv = qobject_cast<AudioClipView*>(obj);
		}
		if (!data.curveView) {
			data.curveView = qobject_cast<CurveView*>(obj);
		}
	}

}

TCommand* SheetView::to_upper_context_level()
{
	ItemBrowserData data;
	collect_item_browser_data(data);

	if (data.currentContext == "TimeLineView" || data.currentContext == "MarkerView") {
        Q_ASSERT(data.tv);
		browse_to_track(data.tv->get_track());
	} else if (data.currentContext == "AudioTrackView") {
        Q_ASSERT(data.atv);
		AudioClipView* nearestClipView = data.atv->get_nearest_audioclip_view(m_session->get_work_location());
		if (nearestClipView) {
			browse_to_audio_clip_view(nearestClipView);
		}
	} else if (data.currentContext == "AudioClipView") {
        Q_ASSERT(data.acv);
		if (data.acv->get_gain_curve_view()->isVisible())
		{
			browse_to_curve_view(data.acv->get_gain_curve_view());
		}
	}

	return nullptr;
}

TCommand* SheetView::to_lower_context_level()
{
	ItemBrowserData data;
	collect_item_browser_data(data);

	if (data.currentContext == "CurveView")
	{
        if (data.acv) {
            browse_to_audio_clip_view(data.acv);
        } else if (data.tv) {
            browse_to_track(data.tv->get_track());
        }
	}
	else if (data.currentContext == "AudioClipView")
	{
		browse_to_track(data.acv->get_clip()->get_track());
	}
	else if (data.currentContext == "AudioTrackView" || data.currentContext == "TBusTrackView")
	{
		browse_to_time_line();
	}

	return nullptr;
}


TCommand* SheetView::browse_to_context_item_below()
{
	ItemBrowserData data;
	collect_item_browser_data(data);

	if (data.currentContext == "CurveView") {
		return nullptr;
	}

	if (data.currentContext == "AudioClipView") {
		while (data.atv) {
			QList<TrackView*> views = get_track_views();
			int index = views.indexOf(data.atv);
			if (index < (views.size() - 1)) {
				data.atv = qobject_cast<AudioTrackView*>(views.at(index + 1));
				if (!data.atv) {
					return nullptr;
				}
				AudioClipView* nearestClipView = data.atv->get_nearest_audioclip_view(m_session->get_work_location());
				if (nearestClipView) {
					browse_to_audio_clip_view(nearestClipView);
					return nullptr;
				}
			} else {
				data.atv = nullptr;
			}
		}

		return nullptr;

	}

	if (data.currentContext == "AudioTrackView" || data.currentContext == "TBusTrackView") {
		QList<TrackView*> views = get_track_views();
		int index = views.indexOf(data.tv);
		if (index < (views.size() - 1)) {
			index += 1;
			browse_to_track(views.at(index)->get_track());
		}

		return nullptr;
	}

	// We're not yet in the viewport, at least not upon a track,
	// browse to top most track
	if (!get_track_views().empty()) {
		browse_to_track(get_track_views().first()->get_track());
	}

	return nullptr;
}

TCommand* SheetView::browse_to_context_item_above()
{
	ItemBrowserData data;
	collect_item_browser_data(data);

	if (data.currentContext == "CurveView") {
		return nullptr;
	}

	if (data.acv) {
		while (data.atv) {
			int index = get_track_views().indexOf(data.atv);
			if (index >= 1) {
                data.atv = qobject_cast<AudioTrackView*>(get_track_views().at(index -1));
                if (data.atv) {
                    AudioClipView* nearestClipView = data.atv->get_nearest_audioclip_view(m_session->get_work_location());
                    if (nearestClipView) {
                        browse_to_audio_clip_view(nearestClipView);
                        return nullptr;
                    }
                }
            }
		}


	} else if (data.tv) {
		int index = get_track_views().indexOf(data.tv);
		if (index >= 1) {
			browse_to_track(get_track_views().at(index -1)->get_track());
		}
	} else {
		// Where not yet in the viewport, at least not upon a track,
		// browse to bottom most track
		if (!get_track_views().empty()) {
			browse_to_track(get_track_views().last()->get_track());
		}
	}

	return nullptr;
}

TCommand* SheetView::browse_to_next_context_item()
{
	QList<ContextItem*> activeList;

	ItemBrowserData data;
	collect_item_browser_data(data);

	if (data.currentContext == "TimeLineView" || data.currentContext == "MarkerView") {
		MarkerView* markerView = m_tlvp->get_timeline_view()->get_marker_view_after(m_session->get_work_location());
		if (!markerView) {
			return nullptr;
		}
		browse_to_marker_view(markerView);

	}
	if (data.currentContext == "CurveView") {
        Q_ASSERT(data.curveView);
        CurveNodeView* nodeView = data.curveView->get_node_view_after(m_session->get_work_location());
		if (!nodeView) {
			return nullptr;
		}
		browse_to_curve_node_view(nodeView);
		return nullptr;
	}

	if (data.currentContext == "AudioClipView") {
        Q_ASSERT(data.atv);
        Q_ASSERT(data.acv);
        AudioClip* nextClip = data.atv->get_track()->get_clip_after(data.acv->get_clip()->get_location()->get_start());
		if (!nextClip) {
			return nullptr;
		}

		QList<AudioClipView*> views = data.atv->get_clipviews();
		foreach(AudioClipView* view, views) {
			if (view->get_clip() == nextClip) {
				browse_to_audio_clip_view(view);
				return nullptr;
			}
		}
	}

	if (data.currentContext == "AudioTrackView") {
		to_upper_context_level();
		return nullptr;
	}

	if (activeList.empty()) {
		return nullptr;
	}

	activeList.append(this);
	cpointer().set_active_context_items_by_keyboard_input(activeList);


	return nullptr;
}

TCommand* SheetView::browse_to_previous_context_item()
{
	QList<ContextItem*> activeList;

	ItemBrowserData data;
	collect_item_browser_data(data);

	if (data.currentContext == "TimeLineView" || data.currentContext == "MarkerView") {
        Q_ASSERT(m_tlvp);
		MarkerView* markerView = m_tlvp->get_timeline_view()->get_marker_view_before(m_session->get_work_location());
		if (!markerView) {
			return nullptr;
		}
		browse_to_marker_view(markerView);

	}

	if (data.currentContext == "CurveView") {
        CurveNodeView* nodeView = data.curveView->get_node_view_before(m_session->get_work_location());
		if (!nodeView) {
			return nullptr;
		}
		browse_to_curve_node_view(nodeView);
		return nullptr;
	}

	if (data.currentContext == "AudioClipView") {
        AudioClip* nextClip = data.atv->get_track()->get_clip_before(data.acv->get_clip()->get_location()->get_start());
		if (!nextClip) {
			return nullptr;
		}

		QList<AudioClipView*> views = data.atv->get_clipviews();
		foreach(AudioClipView* view, views) {
			if (view->get_clip() == nextClip) {
				browse_to_audio_clip_view(view);
				return nullptr;
			}
		}
	}

	if (data.currentContext == "AudioTrackView") {
		to_upper_context_level();
		return nullptr;
	}

	if (activeList.empty()) {
		return nullptr;
	}

	activeList.append(this);
	cpointer().set_active_context_items_by_keyboard_input(activeList);

	return nullptr;
}

void SheetView::center_in_view(ViewItem *item, enum Qt::AlignmentFlag flag)
{
	if (flag == Qt::AlignHCenter) {
        set_hscrollbar_value(int(item->scenePos().x() - m_clipsViewPort->width() / 2));
	} else if (flag == Qt::AlignVCenter) {
        set_vscrollbar_value(int(item->scenePos().y() + (item->boundingRect().height() / 2) - (m_clipsViewPort->height() / 2)));
	}
}

void SheetView::keyboard_move_canvas_cursor_to_location(TTimeRef location, qreal sceneY)
{
    m_session->set_work_at(location);

	int x = m_clipsViewPort->mapFromScene(m_workCursor->scenePos()).x();
	int y = m_clipsViewPort->mapFromScene(0, sceneY).y();

	int canvasWidth = m_clipsViewPort->width();
	int nearBorderMargin = 50;
	if (nearBorderMargin > (canvasWidth / 4))
	{
		nearBorderMargin = 0;
	}

	if (x < (0 + nearBorderMargin) || x > (canvasWidth - nearBorderMargin)) {
		center_in_view(m_workCursor, Qt::AlignHCenter);
	}

	// y is the translation of sceneY to the viewport. if y is outside the
	// viewport area, then use sceneY (!!) to set the vertical scrollbar
	// since the vertical scrollbar range == scene height range.
	if (y < 0 || y > m_clipsViewPort->height()) {
        set_vscrollbar_value(int(sceneY - m_clipsViewPort->height() / 2));
	}

	QPoint pos = m_clipsViewPort->mapFromScene(location / timeref_scalefactor, sceneY);
    cpointer().store_canvas_cursor_position(pos);

    m_canvasCursor->set_text(TTimeRef::timeref_to_text(location, timeref_scalefactor));
    do_keyboard_canvas_cursor_move(QPointF(location / timeref_scalefactor, sceneY));
}

QList<TrackView*> SheetView::get_track_views() const
{
    QList<TrackView*> views;
    if (m_sheetMasterOutView) {
		views.append(m_sheetMasterOutView);
	}
	if (m_projectMasterOutView) {
		views.append(m_projectMasterOutView);
	}
    views.append(m_audioTrackViews);
    views.append(m_busTrackViews);

    return views;
}

int SheetView::get_track_height(Track *track) const
{
	return m_session->get_track_height(track->get_id());
}

TCommand* SheetView::edit_properties()
{
	bool ok;
	QString text = QInputDialog::getText(m_clipsViewPort, tr("Edit Sheet Name"),
					tr("Enter new name"),
					QLineEdit::Normal, m_session->get_name(), &ok);
	if (ok && !text.isEmpty()) {
		m_session->set_name(text);
	}

	return nullptr;
}

void SheetView::set_cursor_shape(const QString& shape, int alignment)
{
    m_canvasCursor->set_cursor_shape(shape, alignment);
}

void SheetView::set_edit_cursor_text(const QString &text, int mseconds)
{
    m_canvasCursor->set_text(text, mseconds);
}

void SheetView::set_canvas_cursor_pos(QPointF pos,  AbstractViewPort::CursorMoveReason reason)
{
    if (reason == AbstractViewPort::CursorMoveReason::KEYBOARD_NAVIGATION) {
        do_keyboard_canvas_cursor_move(pos);
    } else {
        m_canvasCursor->set_pos(pos);
    }
}

void SheetView::do_keyboard_canvas_cursor_move(const QPointF &position)
{
    PENTER;

    QPointF diffPos = m_canvasCursor->get_pos() - position;

    int animDuration = int(diffPos.manhattanLength() * 0.3);
    // do not even bother animating a movement if the distance is real small
    if (animDuration < 50) {
        m_canvasCursor->set_pos(position);
        return;
    }
    if (animDuration > 250) {
        animDuration = 250;
    }

    if (m_canvasCursorMoveAnimation->state() == QPropertyAnimation::Running) {
        m_canvasCursorMoveAnimation->stop();
    }

    m_canvasCursorMoveAnimation->setStartValue(m_canvasCursor->get_pos());
    m_canvasCursorMoveAnimation->setEndValue(position);
    m_canvasCursorMoveAnimation->setDuration(animDuration);
    m_canvasCursorMoveAnimation->start();
}

void SheetView::mouse_hover_move_event()
{
   set_canvas_cursor_pos(cpointer().scene_pos(), AbstractViewPort::CursorMoveReason::MOUSE_MOVE_EVENT);
}

void SheetView::context_changed()
{
	if (!m_clipsViewPort->isVisible())
	{
		return;
	}

	ItemBrowserData data;
	collect_item_browser_data(data);

    QList<ContextItem*> items = cpointer().get_active_context_items();

    if (!items.isEmpty()) {
        foreach(ContextItem * item, items) {
            QString cursorShape = cursor_dict()->value(item->metaObject()->className(), "");
            if (!cursorShape.isEmpty()) {
                cpointer().set_canvas_cursor_shape(cursorShape, Qt::AlignTop | Qt::AlignHCenter);
                break;
            }
        }
    } else {
    PERROR("cpointer returned empty context item list")
    }
}

void SheetView::calculate_cursor_dict()
{
	m_cursorsDict.insert("AudioClipView", "C");
    m_cursorsDict.insert("AudioTrackView", "T");
    m_cursorsDict.insert("AudioTrackPanelView", "T");
    m_cursorsDict.insert("TBusTrackView", "B");
    m_cursorsDict.insert("TBusTrackPanelView", "B");
    m_cursorsDict.insert("PluginView", "P");
	m_cursorsDict.insert("FadeCurveView", "F");
	m_cursorsDict.insert("CurveView", "~");
    m_cursorsDict.insert("CurveNodeView", "N");
    m_cursorsDict.insert("SheetView", "S");
    m_cursorsDict.insert("MarkerView", "M");
    m_cursorsDict.insert("TrackPanelView", "T");
}
