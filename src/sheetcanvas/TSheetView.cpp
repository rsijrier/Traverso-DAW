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

#include "TAudioThreadMessageQueue.h"
#include "TConfig.h"
#include "TCurve.h"
#include "TInputEventDispatcher.h"
#include "TSheet.h"
#include "TAudioClip.h"
#include "TSnapList.h"
#include "TAudioTrack.h"
#include "TTimeLineMarker.h"
#include "TBusTrack.h"
#include "TContextPointer.h"
#include "TThemer.h"
#include "TAudioClipView.h"
#include "TCurveView.h"
#include "TCurveNodeView.h"
#include "TTimeLineMarkerView.h"
#include "TSheetView.h"
#include "TSheetWidget.h"
#include "TAudioTrackView.h"
#include "TBusTrackView.h"
#include "TTrackPanelView.h"
#include "TWorkCursorView.h"
#include "TClipsViewPort.h"
#include "TTimeLineRulerViewPort.h"
#include "TTimeLineRulerView.h"
#include "TTrackPanelViewPort.h"
#include "TCanvasCursor.h"
#include "TSession.h"
#include "TClipsViewPort.h"
#include "TMoveCommand.h"
#include "TLocation.h"

#include "TProjectManager.h"
#include "TProject.h"

#include <Debugger.h>

QHash<QString, QString> TSheetView::m_cursorsDict;

TSheetView::TSheetView(TSheetWidget* sheetwidget,
	TClipsViewPort* viewPort,
	TTrackPanelViewPort* tpvp,
	TTimeLineRulerViewPort* tlvp,
	TSession* session)
	: TViewItem(nullptr, session)
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
    m_contextPointer = &cpointer();

	m_clipsViewPort->scene()->addItem(this);

	m_playCursor = new TPlayHeadView(this, m_session, m_clipsViewPort);
	m_workCursor = new TWorkCursorView(this, m_session);
    m_canvasCursor = new TCanvasCursor(this);
    scene()->addItem(m_canvasCursor);

    m_canvasCursorMoveAnimation = new QPropertyAnimation(m_canvasCursor, "position", this);
    m_canvasCursorMoveAnimation->setEasingCurve(QEasingCurve::InOutQuad);

	TSheet* sheet = qobject_cast<TSheet*>(m_session);

	if (m_session->is_project_session()) {
		m_projectMasterOutView = new TBusTrackView(this, pm().get_project()->get_master_out_bus_track());
        connect(m_projectMasterOutView, &TBusTrackView::totalTrackHeightChanged, this, &TSheetView::layout_tracks);
	}
	if (sheet) {
		m_sheetMasterOutView = new TBusTrackView(this, m_session->get_master_out_bus_track());
        connect(m_sheetMasterOutView, &TBusTrackView::totalTrackHeightChanged, this, &TSheetView::layout_tracks);
	}

	connect(m_session, &TSession::workingPosChanged, m_workCursor, &TWorkCursorView::update_position);
	connect(m_session, &TSession::transportStarted, this, &TSheetView::follow_play_head);
	connect(m_session, &TSession::transportLocationChanged, this, &TSheetView::transport_position_set);
	connect(m_session, &TSession::workingPosChanged, this, &TSheetView::stop_follow_play_head);
	connect(m_session, &TSession::verticalScrollBarValueChanged, this, &TSheetView::session_vertical_scrollbar_position_changed);
	connect(m_session, &TSession::horizontalScrollBarValueChanged, this, &TSheetView::session_horizontal_scrollbar_position_changed);

    connect(&tsmp(), &TAudioThreadMessageQueue::batchTransactionStarted, this, [this]() {
        m_addRemoveBatchActive = true;
    });

    connect(&tsmp(), &TAudioThreadMessageQueue::batchTransactionFinished, this, [this]() {
        m_addRemoveBatchActive = false;

        m_contextPointer->request_viewport_to_detect_items_below_cursor();
    });

	m_clipsViewPort->scene()->addItem(m_playCursor);
	m_clipsViewPort->scene()->addItem(m_workCursor);

	m_clipsViewPort->setSceneRect(0, 0, MAX_CANVAS_WIDTH, MAX_CANVAS_HEIGHT);
	m_tlvp->setSceneRect(0, -TIMELINE_HEIGHT, MAX_CANVAS_WIDTH, 0);
    m_tpvp->setSceneRect(-m_tpvp->width(), 0, 0, MAX_CANVAS_HEIGHT);

	// Set up the viewports scale factor, and our timeref_scalefactor / m_peakCacheZoomFactor
	// Needed for our childs TrackView, AudioClipView, TimeLines MarkerViews etc which are created below.
	scale_factor_changed();

	connect(m_session, &TSession::hzoomChanged, this, &TSheetView::scale_factor_changed);
	connect(m_session, &TSession::tempFollowChanged, this, &TSheetView::set_follow_state);
    connect(m_session, &TSession::trackAdded, this, &TSheetView::add_new_track_view);
    connect(m_session, &TSession::trackRemoved, this, &TSheetView::remove_track_view);
	connect(m_session, &TSession::lastFramePositionChanged, this, &TSheetView::update_scrollbars);
	connect(m_hScrollBar, &QScrollBar::sliderMoved, this, &TSheetView::stop_follow_play_head);
	connect(m_hScrollBar, &QScrollBar::actionTriggered, this, &TSheetView::hscrollbar_action);
	connect(m_hScrollBar, &QScrollBar::valueChanged, this, &TSheetView::hscrollbar_value_changed);
	connect(m_vScrollBar, &QScrollBar::valueChanged, m_clipsViewPort->verticalScrollBar(), &QScrollBar::setValue);

    connect(m_contextPointer, &TContextPointer::contextChanged, this, &TSheetView::context_changed);

	// fill the view with trackviews, add_new_trackview()
	// doesn't yet layout the new tracks.
	foreach(TTrack* track, m_session->get_tracks()) {
		add_new_track_view(track);
	}

	// this will call layout_tracks() for us too
	// which will continue now, due m_viewportReady is true now
    TSheetView::load_theme_data();

	// Everything is in place to scroll to the last position
	// we were at, at closing this view.
	QPoint p = m_session->get_scrollbar_xy();
	m_session->set_scrollbar_x(p.x());
	m_session->set_scrollbar_y(p.y());
}

TSheetView::~TSheetView()
{
}

void TSheetView::scale_factor_changed( )
{
	qreal zoom = m_session->get_hzoom();
    if (TraversoDAW::Float::equals_0(zoom)) {
//		PERROR("Session %s return 0 hzoom factor!", m_session->get_name().toLatin1().data());
		// Woopsy, zoom can't be zero, if we allow that, timeref_scalefactor
		// will be zero too, and we use timeref_scalefactor as a divider so...:
		zoom = config().get_property("Sheet", "hzoomLevel", 8192).toInt();
	}
    timeref_scalefactor = qint64(zoom * (TTimeRef::UNIVERSAL_SAMPLE_RATE / 44100));
	m_tlvp->scale_factor_changed();
	m_clipsViewPort->set_timeref_scale_factor(timeref_scalefactor);
	m_tlvp->set_timeref_scale_factor(timeref_scalefactor);
	m_tpvp->set_timeref_scale_factor(timeref_scalefactor);

	update_tracks_bounding_rect();
}

TAudioTrackView* TSheetView::get_audio_trackview_at_scene_pos( QPointF point )
{
    TAudioTrackView* view = nullptr;
	QList<QGraphicsItem*> views = m_clipsViewPort->items(m_clipsViewPort->mapFromScene(point));

	for (int i=0; i<views.size(); ++i) {
        view = dynamic_cast<TAudioTrackView*>(views.at(i));
		if (view) {
			return view;
		}
	}
    return  nullptr;

}

TTrackView* TSheetView::get_trackview_at_scene_pos( QPointF point )
{
	QList<QGraphicsItem*> views = m_clipsViewPort->items(m_clipsViewPort->mapFromScene(point));

	for (int i=0; i<views.size(); ++i) {
		TTrackView* view = dynamic_cast<TTrackView*>(views.at(i));
		if (view) {
			return view;
		}
		TTrackPanelView* tpv = dynamic_cast<TTrackPanelView*>(views.at(i));
		if (tpv)
		{
			return tpv->get_track_view();
		}
	}
    return  nullptr;

}


void TSheetView::move_trackview_up(TTrackView *trackView)
{
    int index = trackView->get_track()->get_sort_index();
    if (index == 0 || trackView->get_track() == m_session->get_master_out_bus_track()) {
        // can't move any further up
        return;
    }

    TAudioTrackView* atv = qobject_cast<TAudioTrackView*>(trackView);
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

void TSheetView::move_trackview_down(TTrackView *trackView)
{
    int index = trackView->get_track()->get_sort_index();
    if (index >= m_audioTrackViews.size() || trackView->get_track() == m_session->get_master_out_bus_track()) {
        // can't move any further down
        return;
    }

    TAudioTrackView* atv = qobject_cast<TAudioTrackView*>(trackView);
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

void TSheetView::to_bottom(TTrackView *trackView)
{
    TAudioTrackView* atv = qobject_cast<TAudioTrackView*>(trackView);
	TBusTrackView* btv = qobject_cast<TBusTrackView*>(trackView);

	if (atv) {
		QList<TTrackView*> list = m_audioTrackViews;
		list.removeAll(atv);
		for(int i=0; i<list.size(); i++) {
			list.at(i)->get_track()->set_sort_index(i);
		}
		atv->get_track()->set_sort_index(list.size());
	}

	if (btv) {
		QList<TTrackView*> list = m_busTrackViews;
		list.removeAll(atv);

		for(int i=0; i<list.size(); i++) {
			list.at(i)->get_track()->set_sort_index(i);
		}
		btv->get_track()->set_sort_index(list.size());
	}


	layout_tracks();
}

void TSheetView::to_top(TTrackView *trackView)
{
	int index = trackView->get_track()->get_sort_index();
	if (index == 0) {
		// it's allready topmost, don't do anything
		return;
	}

    TAudioTrackView* atv = qobject_cast<TAudioTrackView*>(trackView);
	TBusTrackView* btv = qobject_cast<TBusTrackView*>(trackView);

	if (atv) {
		QList<TTrackView*> list = m_audioTrackViews;
		list.removeAll(atv);
		atv->get_track()->set_sort_index(0);

		for(int i=0; i<list.size(); i++) {
			list.at(i)->get_track()->set_sort_index(i + 1);
		}
	}

	if (btv) {
		QList<TTrackView*> list = m_busTrackViews;
		list.removeAll(atv);
		btv->get_track()->set_sort_index(0);

		for(int i=0; i<list.size(); i++) {
			list.at(i)->get_track()->set_sort_index(i + 1);
		}
	}


	layout_tracks();
}

void TSheetView::add_new_track_view(TTrack* track)
{
    TTrackView* view = nullptr;

	TAudioTrack* audioTrack = qobject_cast<TAudioTrack*>(track);
	TBusTrack* busTrack = qobject_cast<TBusTrack*>(track);

	int sortIndex = track->get_sort_index();

	if (audioTrack) {
        view = new TAudioTrackView(this, audioTrack);
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
        connect(view, &TTrackView::totalTrackHeightChanged, this, &TSheetView::layout_tracks);
    }

	layout_tracks();
}

void TSheetView::remove_track_view(TTrack* track)
{
	QList<TTrackView*> views;
	views.append(m_audioTrackViews);
	views.append(m_busTrackViews);

	foreach(TTrackView* view, views) {
		if (view->get_track() == track) {
			TTrackPanelView* panel = view->get_panel_view();
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

void TSheetView::update_scrollbars()
{
    int width = int(m_session->get_last_location() / timeref_scalefactor) - (m_clipsViewPort->width() / 4);
	if (width < m_clipsViewPort->width() / 4) {
		width = m_clipsViewPort->width() / 4;
	}

	m_hScrollBar->setRange(0, width);
	m_hScrollBar->setSingleStep(m_clipsViewPort->width() / 10);
	m_hScrollBar->setPageStep(m_clipsViewPort->width());

	m_vScrollBar->setRange(0, m_sceneHeight - m_clipsViewPort->height() + 8);
	m_vScrollBar->setSingleStep(m_clipsViewPort->height() / 10);
	m_vScrollBar->setPageStep(m_clipsViewPort->height());

	bool needsVScrollBar = (m_sceneHeight - m_clipsViewPort->height() + 8 > 0);
	m_vScrollBar->setVisible(needsVScrollBar);

    m_playCursor->set_bounding_rect(QRectF(0, 0, 4, m_vScrollBar->maximum() + m_clipsViewPort->height()));
	m_playCursor->update_position();
	m_workCursor->set_bounding_rect(QRectF(0, 0, 1, m_vScrollBar->maximum() + m_clipsViewPort->height()));
	m_workCursor->update_position();

	set_snap_range(m_hScrollBar->value());
}

void TSheetView::hscrollbar_value_changed(int value)
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
        TMoveCommand* s = qobject_cast<TMoveCommand*>(ied().get_holding_command());
		if (!s) {
//			ied().jog();
		}
	} else {
		m_clipsViewPort->horizontalScrollBar()->setValue(value);
	}

	set_snap_range(value);
}

void TSheetView::clipviewport_resize_event()
{
	update_scrollbars();
}

void TSheetView::vzoom(qreal factor)
{
	PENTER;
	for (int i=0; i<m_audioTrackViews.size(); ++i) {
		TTrackView* view = m_audioTrackViews.at(i);
		TTrack* track = view->get_track();
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


TCommand* TSheetView::toggle_expand_all_tracks(int height)
{
	if (height < 0) {
		if (m_meanTrackHeight > m_trackMinimumHeight) {
			foreach(TTrackView* view, get_track_views()) {
				m_session->set_track_height(view->get_track()->get_id(), m_trackMinimumHeight);
			}
		} else {
			foreach(TTrackView* view, get_track_views()) {
				m_session->set_track_height(view->get_track()->get_id(), TTrack::INITIAL_HEIGHT);
			}
		}
	} else {
		foreach(TTrackView* view, get_track_views()) {
			m_session->set_track_height(view->get_track()->get_id(), height);
		}
	}

	update_tracks_bounding_rect();

    return nullptr;
}

void TSheetView::set_track_height(TTrackView *view, int newheight)
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

void TSheetView::hzoom(qreal factor)
{
	PENTER;
	m_session->scale_hzoom(factor);
	center();
}


void TSheetView::layout_tracks()
{    
	int verticalposition = m_trackTopIndent;
	int totalTrackHeightPrimaryLanes = 0;

    std::sort(m_audioTrackViews.begin(), m_audioTrackViews.end(), [&](TTrackView* left, TTrackView* right) {
        return left->get_track()->get_sort_index() < right->get_track()->get_sort_index();
    });
    std::sort(m_busTrackViews.begin(), m_busTrackViews.end(), [&](TTrackView* left, TTrackView* right) {
        return left->get_track()->get_sort_index() < right->get_track()->get_sort_index();
    });

    QList<TTrackView*> views = m_audioTrackViews;
    views.append(m_busTrackViews);
    if (m_sheetMasterOutView) {
        views.append(m_sheetMasterOutView);
    }


	for (int i=0; i<views.size(); ++i) {
		TTrackView* view = views.at(i);
		view->move_to(0, verticalposition);
		verticalposition += view->get_total_height() + m_trackSeperatingHeight;
		totalTrackHeightPrimaryLanes += get_track_height(view->get_track());
	}

	m_sceneHeight = verticalposition;

	// + 1, one for sheet master!
    m_meanTrackHeight = qreal(totalTrackHeightPrimaryLanes) / (m_audioTrackViews.size() + m_busTrackViews.size() + 1);

	update_scrollbars();
}

void TSheetView::update_tracks_bounding_rect()
{
	QList<TTrackView*> views = get_track_views();

	for (int i=0; i<views.size(); ++i) {
		views.at(i)->calculate_bounding_rect();
	}

	layout_tracks();
}


TCommand* TSheetView::center()
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


void TSheetView::transport_position_set()
{
	if (!m_session->is_transport_rolling()) {
		m_playCursor->update_position();
	}
}


void TSheetView::stop_follow_play_head()
{
    m_session->set_temp_follow_state(false);
}


void TSheetView::follow_play_head()
{
    m_session->set_temp_follow_state(true);
}


void TSheetView::set_follow_state(bool state)
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

TCommand* TSheetView::goto_begin()
{
	stop_follow_play_head();
	m_session->set_work_at(TTimeRef());
	center();
    return nullptr;
}


TCommand* TSheetView::goto_end()
{
	stop_follow_play_head();
	TTimeRef lastlocation = m_session->get_last_location();
	m_session->set_work_at(lastlocation);
	center();
    return nullptr;
}


TTrackPanelViewPort* TSheetView::get_trackpanel_view_port( ) const
{
	return m_tpvp;
}

TClipsViewPort * TSheetView::get_clips_viewport() const
{
	return m_clipsViewPort;
}

TTimeLineRulerViewPort* TSheetView::get_timeline_viewport() const
{
	return m_tlvp;
}

TCommand * TSheetView::touch( )
{
    TViewPort* viewPort = dynamic_cast<TViewPort*>(m_contextPointer->get_viewport());

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
        x = m_contextPointer->on_first_input_event_x();
	} else {
        x = m_contextPointer->mouse_viewport_x();
	}

    m_session->set_work_at(TTimeRef(qRound(viewPort->map_to_scene(QPoint(x, 0)).x()) * timeref_scalefactor));

    return nullptr;
}

TCommand * TSheetView::touch_play_cursor( )
{
    if (m_contextPointer->get_viewport() == m_tpvp) {
		return ied().did_not_implement();
	}
	int x;
    if (!m_contextPointer->get_viewport()) {
        x = m_contextPointer->on_first_input_event_x();
	} else {
        x = m_contextPointer->mouse_viewport_x();
	}
	m_session->set_transport_location(TTimeRef(qRound(m_clipsViewPort->mapToScene(x, 0).x()) * timeref_scalefactor));

	return nullptr;
}

void TSheetView::set_snap_range(int /*start*/)
{
// 	printf("SheetView::set_snap_range\n");
    m_session->get_snap_list()->set_range(TTimeRef(), m_session->get_last_location(), timeref_scalefactor);
// TODO: setting range like this makes snaplist->next_snap_pos broken
    // also, do we really want to limit snaplist prev/next snap location to be bound
    // the the viewport range ?
    // m_session->get_snap_list()->set_range(TTimeRef(start * timeref_scalefactor),
    // 			TTimeRef((start + m_clipsViewPort->viewport()->width()) * timeref_scalefactor),
    // 			timeref_scalefactor);
}

TCommand* TSheetView::scroll_right_by(int delta)
{
	PENTER3;
	stop_follow_play_head();
	set_hscrollbar_value(m_clipsViewPort->horizontalScrollBar()->value() + delta);
    return nullptr;
}

TCommand* TSheetView::scroll_left_by(int delta)
{
	PENTER3;
	stop_follow_play_head();
	set_hscrollbar_value(m_clipsViewPort->horizontalScrollBar()->value() - delta);
    return nullptr;
}

TCommand* TSheetView::scroll_up_by(int delta)
{
	PENTER3;
	set_vscrollbar_value(m_clipsViewPort->verticalScrollBar()->value() - delta);
    return nullptr;
}

TCommand* TSheetView::scroll_down_by(int delta)
{
	PENTER3;
	set_vscrollbar_value(m_clipsViewPort->verticalScrollBar()->value() + delta);
    return nullptr;
}

// scroll_up and scroll_down are still referenced from TraversoCommands::load()
TCommand* TSheetView::scroll_up( )
{
	PENTER3;
	return scroll_up_by(int(m_meanTrackHeight * 0.75));
}

TCommand* TSheetView::scroll_down( )
{
	PENTER3;
	return scroll_down_by(int(m_meanTrackHeight * 0.75));
}


int TSheetView::hscrollbar_value() const
{
	return m_clipsViewPort->horizontalScrollBar()->value();
}

void TSheetView::hscrollbar_action(int action)
{
	if (action == QAbstractSlider::SliderPageStepAdd || action == QAbstractSlider::SliderPageStepSub) {
		stop_follow_play_head();
	}
}

int TSheetView::vscrollbar_value() const
{
	return m_clipsViewPort->verticalScrollBar()->value();
}

void TSheetView::load_theme_data()
{
	m_trackSeperatingHeight = themer()->get_property("Sheet:track:seperatingheight", 0).toInt();
    m_trackMinimumHeight = 55;
    m_trackMaximumHeight = 1024;
	m_trackTopIndent = themer()->get_property("Sheet:track:topindent", 6).toInt();

	m_clipsViewPort->setBackgroundBrush(themer()->get_color("Sheet:background"));
	m_tpvp->setBackgroundBrush(themer()->get_brush("TrackPanel:background", QPoint(0, 0), QPoint(0, m_tpvp->height())));

	update_tracks_bounding_rect();
}

TCommand * TSheetView::center_playhead( )
{
	TTimeRef centerX = m_session->get_transport_location();
	set_hscrollbar_value(int(centerX / timeref_scalefactor - m_clipsViewPort->width() / 2));

	follow_play_head();

    return nullptr;
}

void TSheetView::set_hscrollbar_value(int value)
{
	m_session->set_scrollbar_x(value);
}

void TSheetView::set_vscrollbar_value(int value)
{
	if (value > m_vScrollBar->maximum()) {
		value = m_vScrollBar->maximum();
	}
	if (value < 0) {
		value = 0;
	}
	m_session->set_scrollbar_y(value);
}

void TSheetView::session_vertical_scrollbar_position_changed()
{
	QPoint p = m_session->get_scrollbar_xy();

	m_clipsViewPort->verticalScrollBar()->setValue(p.y());
	m_vScrollBar->setValue(p.y());
}

void TSheetView::session_horizontal_scrollbar_position_changed()
{
	QPoint p = m_session->get_scrollbar_xy();

	m_clipsViewPort->horizontalScrollBar()->setValue(p.x());
	m_hScrollBar->setValue(p.x());
}

void TSheetView::browse_to_track(TTrack *track)
{
	QList<TTrackView*> views = get_track_views();
	if (m_sheetMasterOutView) {
		views.append(m_sheetMasterOutView);
	}
	if (m_projectMasterOutView) {
		views.append(m_projectMasterOutView);
	}

	foreach(TTrackView* view, views) {
		if (view->get_track() == track) {
			QList<TContextItem*> list;
			list.append(view);
			list.append(this);

            m_contextPointer->set_active_context_items_by_keyboard_input(list);

            keyboard_move_canvas_cursor_to_location(m_session->get_work_location(), view->scenePos().y() + view->boundingRect().height() / 2);

			return;
		}
	}
}

void TSheetView::browse_to_audio_clip_view(TAudioClipView* acv)
{
	QList<TContextItem*> activeList;

	activeList.append(acv);
	activeList.append(acv->get_audio_track_view());
	activeList.append(this);

    if (m_canvasCursor->get_pos().x() > acv->scenePos().x() && (m_canvasCursor->get_pos().x() < (acv->scenePos().x() + acv->boundingRect().width()))) {
        keyboard_move_canvas_cursor_to_location(TTimeRef(m_canvasCursor->get_pos().x() * timeref_scalefactor), acv->scenePos().y() + acv->boundingRect().height() / 2);
    } else {
        keyboard_move_canvas_cursor_to_location(TTimeRef((acv->scenePos().x() + acv->boundingRect().width() / 2) * timeref_scalefactor), acv->scenePos().y() + acv->boundingRect().height() / 2);
    }


    m_contextPointer->set_active_context_items_by_keyboard_input(activeList);
}

void TSheetView::browse_to_curve_view(TCurveView *curveView)
{
	QList<TContextItem*> activeList;
	TAudioClipView* acv = static_cast<TAudioClipView*>(curveView->parentItem());
	activeList.append(curveView);
	activeList.append(acv);
	activeList.append(acv->get_audio_track_view());
	activeList.append(this);
    m_contextPointer->set_active_context_items_by_keyboard_input(activeList);
}

void TSheetView::browse_to_marker_view(TTimeLineMarkerView *markerView)
{
	if (!markerView) {
		return;
	}

    QList<TContextItem*> contexts = m_contextPointer->get_active_context_items();
	TTimeLineMarkerView* view;
	foreach(TContextItem* item, contexts) {
		view = qobject_cast<TTimeLineMarkerView*>(item);
		if (view) {
            m_contextPointer->remove_from_active_context_list(item);
			contexts.removeAll(item);
		}
	}

    keyboard_move_canvas_cursor_to_location(TTimeRef(markerView->get_marker()->get_location()->get_start()), m_contextPointer->scene_y());

	contexts.prepend(markerView);
    m_contextPointer->set_active_context_items_by_keyboard_input(contexts);
}

void TSheetView::browse_to_curve_node_view(TCurveNodeView *nodeView)
{
    QList<TContextItem*> activeList;
	TCurveView* curveView = nodeView->get_curve_view();
    curveView->update_softselected_node(nodeView->scenePos());

	TAudioClipView* acv = static_cast<TAudioClipView*>(curveView->parentItem());
	activeList.append(curveView);
	activeList.append(acv);
	activeList.append(acv->get_audio_track_view());
	activeList.append(this);

    keyboard_move_canvas_cursor_to_location(TTimeRef(nodeView->get_curve_node()->get_when()) + curveView->get_curve()->get_start_offset(),
			   nodeView->scenePos().y() + nodeView->boundingRect().height() / 2);

    m_contextPointer->set_active_context_items_by_keyboard_input(activeList);

}

TCommand* TSheetView::browse_to_time_line()
{
    QList<TContextItem*> items = m_contextPointer->get_active_context_items();
	items.prepend(m_tlvp->get_timeline_view());

    m_contextPointer->set_active_context_items_by_keyboard_input(items);

	return nullptr;
}

void TSheetView::collect_item_browser_data(ItemBrowserData &data)
{
    QList<TContextItem*> list = m_contextPointer->get_active_context_items();

	if (!list.empty()) {
		data.currentContext = list.first()->metaObject()->className();
	}

	foreach(TContextItem* obj, list) {
        if (!data.timeLineView) {
			data.timeLineView = qobject_cast<TTimeLineRulerView*>(obj);
		}
		if (!data.markerView) {
			data.markerView = qobject_cast<TTimeLineMarkerView*>(obj);
		}
		if (!data.tv) {
			data.tv = qobject_cast<TTrackView*>(obj);
		}
		if (!data.atv) {
            data.atv = qobject_cast<TAudioTrackView*>(obj);
		}
		if (!data.acv) {
			data.acv = qobject_cast<TAudioClipView*>(obj);
		}
		if (!data.curveView) {
			data.curveView = qobject_cast<TCurveView*>(obj);
		}
	}

}

TCommand* TSheetView::to_upper_context_level()
{
	ItemBrowserData data;
	collect_item_browser_data(data);

    if (data.currentContext == "TTimeLineRulerView" || data.currentContext == "TTimeLineMarkerView") {
        Q_ASSERT(data.tv);
		browse_to_track(data.tv->get_track());
    } else if (data.currentContext == "TAudioTrackView") {
        Q_ASSERT(data.atv);
		TAudioClipView* nearestClipView = data.atv->get_nearest_audioclip_view(m_session->get_work_location());
		if (nearestClipView) {
			browse_to_audio_clip_view(nearestClipView);
		}
    } else if (data.currentContext == "TAudioClipView") {
        Q_ASSERT(data.acv);
		if (data.acv->get_gain_curve_view()->isVisible())
		{
			browse_to_curve_view(data.acv->get_gain_curve_view());
		}
	}

	return nullptr;
}

TCommand* TSheetView::to_lower_context_level()
{
	ItemBrowserData data;
	collect_item_browser_data(data);

    if (data.currentContext == "TCurveView")
	{
        if (data.acv) {
            browse_to_audio_clip_view(data.acv);
        } else if (data.tv) {
            browse_to_track(data.tv->get_track());
        }
	}
    else if (data.currentContext == "TAudioClipView")
	{
		browse_to_track(data.acv->get_clip()->get_track());
	}
    else if (data.currentContext == "TAudioTrackView" || data.currentContext == "TBusTrackView")
	{
		browse_to_time_line();
	}

	return nullptr;
}


TCommand* TSheetView::browse_to_context_item_below()
{
	ItemBrowserData data;
	collect_item_browser_data(data);

    if (data.currentContext == "TCurveView") {
		return nullptr;
	}

    if (data.currentContext == "TAudioClipView") {
		while (data.atv) {
			QList<TTrackView*> views = get_track_views();
			int index = views.indexOf(data.atv);
			if (index < (views.size() - 1)) {
                data.atv = qobject_cast<TAudioTrackView*>(views.at(index + 1));
				if (!data.atv) {
					return nullptr;
				}
				TAudioClipView* nearestClipView = data.atv->get_nearest_audioclip_view(m_session->get_work_location());
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

    if (data.currentContext == "TAudioTrackView" || data.currentContext == "TBusTrackView") {
		QList<TTrackView*> views = get_track_views();
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

TCommand* TSheetView::browse_to_context_item_above()
{
	ItemBrowserData data;
	collect_item_browser_data(data);

    if (data.currentContext == "TCurveView") {
		return nullptr;
	}

	if (data.acv) {
		while (data.atv) {
			int index = get_track_views().indexOf(data.atv);
			if (index >= 1) {
                data.atv = qobject_cast<TAudioTrackView*>(get_track_views().at(index -1));
                if (data.atv) {
                    TAudioClipView* nearestClipView = data.atv->get_nearest_audioclip_view(m_session->get_work_location());
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

TCommand* TSheetView::browse_to_next_context_item()
{
	QList<TContextItem*> activeList;

	ItemBrowserData data;
	collect_item_browser_data(data);

    if (data.currentContext == "TTimeLineRulerView" || data.currentContext == "TTimeLineMarkerView") {
		TTimeLineMarkerView* markerView = m_tlvp->get_timeline_view()->get_marker_view_after(m_session->get_work_location());
		if (!markerView) {
			return nullptr;
		}
		browse_to_marker_view(markerView);

	}
	if (data.currentContext == "CurveView") {
        Q_ASSERT(data.curveView);
        TCurveNodeView* nodeView = data.curveView->get_node_view_after(m_session->get_work_location());
		if (!nodeView) {
			return nullptr;
		}
		browse_to_curve_node_view(nodeView);
		return nullptr;
	}

    if (data.currentContext == "TAudioClipView") {
        Q_ASSERT(data.atv);
        Q_ASSERT(data.acv);
        TAudioClip* nextClip = data.atv->get_track()->get_clip_after(data.acv->get_clip()->get_location_start());
		if (!nextClip) {
			return nullptr;
		}

		QList<TAudioClipView*> views = data.atv->get_clipviews();
		foreach(TAudioClipView* view, views) {
			if (view->get_clip() == nextClip) {
				browse_to_audio_clip_view(view);
				return nullptr;
			}
		}
	}

    if (data.currentContext == "TAudioTrackView") {
		to_upper_context_level();
		return nullptr;
	}

	if (activeList.empty()) {
		return nullptr;
	}

	activeList.append(this);
    m_contextPointer->set_active_context_items_by_keyboard_input(activeList);


	return nullptr;
}

TCommand* TSheetView::browse_to_previous_context_item()
{
	QList<TContextItem*> activeList;

	ItemBrowserData data;
	collect_item_browser_data(data);

    if (data.currentContext == "TTimeLineRulerView" || data.currentContext == "TTimeLineMarkerView") {
        Q_ASSERT(m_tlvp);
		TTimeLineMarkerView* markerView = m_tlvp->get_timeline_view()->get_marker_view_before(m_session->get_work_location());
		if (!markerView) {
			return nullptr;
		}
		browse_to_marker_view(markerView);

	}

    if (data.currentContext == "TCurveView") {
        TCurveNodeView* nodeView = data.curveView->get_node_view_before(m_session->get_work_location());
		if (!nodeView) {
			return nullptr;
		}
		browse_to_curve_node_view(nodeView);
		return nullptr;
	}

    if (data.currentContext == "TAudioClipView") {
        TAudioClip* nextClip = data.atv->get_track()->get_clip_before(data.acv->get_clip()->get_location_start());
		if (!nextClip) {
			return nullptr;
		}

		QList<TAudioClipView*> views = data.atv->get_clipviews();
		foreach(TAudioClipView* view, views) {
			if (view->get_clip() == nextClip) {
				browse_to_audio_clip_view(view);
				return nullptr;
			}
		}
	}

    if (data.currentContext == "TAudioTrackView") {
		to_upper_context_level();
		return nullptr;
	}

	if (activeList.empty()) {
		return nullptr;
	}

	activeList.append(this);
    m_contextPointer->set_active_context_items_by_keyboard_input(activeList);

	return nullptr;
}

void TSheetView::center_in_view(TViewItem *item, enum Qt::AlignmentFlag flag)
{
	if (flag == Qt::AlignHCenter) {
        set_hscrollbar_value(int(item->scenePos().x() - m_clipsViewPort->width() / 2));
	} else if (flag == Qt::AlignVCenter) {
        set_vscrollbar_value(int(item->scenePos().y() + (item->boundingRect().height() / 2) - (m_clipsViewPort->height() / 2)));
	}
}

void TSheetView::keyboard_move_canvas_cursor_to_location(const TTimeRef &location, qreal sceneY)
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
    m_contextPointer->store_canvas_cursor_position(pos);

    m_canvasCursor->set_text(TTimeRef::timeref_to_text(location, timeref_scalefactor));
    do_keyboard_canvas_cursor_move(QPointF(location / timeref_scalefactor, sceneY));
}

QList<TTrackView*> TSheetView::get_track_views() const
{
    QList<TTrackView*> views;
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

int TSheetView::get_track_height(TTrack *track) const
{
	return m_session->get_track_height(track->get_id());
}

TCommand* TSheetView::edit_properties()
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

void TSheetView::set_canvas_cursor_type(TContextPointer::CursorType cursorType)
{
    if (m_addRemoveBatchActive) {
        qDebug() << "surpressing canvas cursor type changes during batch processing";
        return;
    }
    m_canvasCursor->set_canvas_cursor_type(cursorType);
}


void TSheetView::set_edit_cursor_text(const QString &text, int mseconds)
{
    m_canvasCursor->set_text(text, mseconds);
}

void TSheetView::set_canvas_cursor_pos(QPointF pos,  TViewPortInterface::CursorMoveReason reason)
{
    if (reason == TViewPortInterface::CursorMoveReason::KEYBOARD_NAVIGATION) {
        do_keyboard_canvas_cursor_move(pos);
    } else {
        m_canvasCursor->set_pos(pos);
    }
}

void TSheetView::set_canvas_cursor_data(const QVariant& data)
{
    // Pass the variant directly down to the graphics scene item
    m_canvasCursor->set_cursor_data(data);
}


void TSheetView::do_keyboard_canvas_cursor_move(const QPointF &position)
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

void TSheetView::mouse_hover_move_event()
{
   set_canvas_cursor_pos(m_contextPointer->scene_pos(), TViewPortInterface::CursorMoveReason::MOUSE_MOVE_EVENT);
}

void TSheetView::context_changed()
{
    if (!m_clipsViewPort->isVisible()) {
        return;
    }

    QList<TContextItem*> items = m_contextPointer->get_active_context_items();

    if (items.isEmpty()) {
        m_canvasCursor->set_canvas_cursor_type(TContextPointer::CursorType::Default);
        m_canvasCursor->set_cursor_data(QVariant());
        return;
    }

    TContextItem* primaryContextItem = items.first();
    if (!primaryContextItem) {
        return;
    }

    QChar overlayLetter = QString(primaryContextItem->metaObject()->className()).at(1);//  primaryContextItem->get_context_char();

    m_canvasCursor->set_canvas_cursor_type(TContextPointer::CursorType::Default);
    m_canvasCursor->set_cursor_data(QVariant::fromValue(overlayLetter));
}


void TSheetView::calculate_cursor_dict()
{
    m_cursorsDict.insert("TAudioClipView", "C");
    m_cursorsDict.insert("TAudioTrackView", "T");
    m_cursorsDict.insert("AudioTrackPanelView", "T");
    m_cursorsDict.insert("TBusTrackView", "B");
    m_cursorsDict.insert("TBusTrackPanelView", "B");
    m_cursorsDict.insert("TAudioPluginView", "P");
    m_cursorsDict.insert("TFadeCurveView", "F");
	m_cursorsDict.insert("CurveView", "~");
    m_cursorsDict.insert("CurveNodeView", "N");
    m_cursorsDict.insert("TSheetView", "S");
    m_cursorsDict.insert("MarkerView", "M");
    m_cursorsDict.insert("TrackPanelView", "T");
    m_cursorsDict.insert("TimeLineView", "R");
}
