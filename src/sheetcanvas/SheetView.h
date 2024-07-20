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

#ifndef SONG_VIEW_H
#define SONG_VIEW_H

#include "ViewItem.h"
#include "AbstractViewPort.h"
#include <QPropertyAnimation>

class AudioClip;
class Sheet;
class AudioClipView;
class AudioTrackView;
class ClipsViewPort;
class CurveView;
class CurveNodeView;
class MarkerView;
class TrackPanelViewPort;
class TimeLineViewPort;
class TimeLineView;
class TrackView;
class TSession;
class SheetWidget;
class AudioTrackView;
class Track;
class PlayHead;
class WorkCursor;
class TCanvasCursor;
class Curve;

struct ItemBrowserData {
	ItemBrowserData() {
		acv = 0;
		atv = 0;
		tv = 0;
		curveView = 0;
		markerView = 0;
		timeLineView = 0;
	}

	TimeLineView* timeLineView;
	MarkerView* markerView;
	TrackView* tv;
	AudioTrackView* atv;
	AudioClipView* acv;
	CurveView* curveView;
	QString currentContext;
};

class SheetView : public ViewItem
{
	Q_OBJECT

public :

	SheetView(SheetWidget* sheetwidget,
			ClipsViewPort* viewPort,
			TrackPanelViewPort* tpvp,
			TimeLineViewPort* tlvp,
			TSession* sheet);
	~SheetView();

    void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) {}
	QRectF boundingRect() const {return QRectF();}

	TSession* get_sheet() const {return m_session;}
	TrackPanelViewPort* get_trackpanel_view_port() const;
	ClipsViewPort* get_clips_viewport() const;
	TimeLineViewPort* get_timeline_viewport() const;
	PlayHead* get_play_cursor() const {return m_playCursor;}
	WorkCursor* get_work_cursor() const {return m_workCursor;}

    AudioTrackView* get_audio_trackview_at_scene_pos(QPointF point);
    TrackView* get_trackview_at_scene_pos(QPointF point);
	QList<TrackView*> get_track_views() const;
	int get_track_height(Track* track) const;
    qreal get_mean_track_height() const {return m_meanTrackHeight;}
	static QHash<QString, QString>* cursor_dict();

	QScrollBar* getVScrollBar() {return m_vScrollBar;}
	QScrollBar* getHScrollBar() {return m_hScrollBar;}

	void load_theme_data();
	void vzoom(qreal scale);
	void hzoom(qreal scale);
	void clipviewport_resize_event();
	int hscrollbar_value() const;
	int vscrollbar_value() const;
	void move_trackview_up(TrackView* trackView);
	void move_trackview_down(TrackView* trackView);
	void to_top(TrackView* trackView);
	void to_bottom(TrackView* trackView);
	void set_track_height(TrackView* view, int newheight);
	void set_hscrollbar_value(int value);
	void set_vscrollbar_value(int value);

	void set_cursor_shape(const QString& shape, int alignment);
	void set_edit_cursor_text(const QString& text, int mseconds=-1);
    void set_canvas_cursor_pos(QPointF pos, AbstractViewPort::CursorMoveReason reason);
    void mouse_hover_move_event();


	void browse_to_track(Track* track);
	void browse_to_audio_clip_view(AudioClipView* acv);
	void browse_to_curve_view(CurveView* curveView);
	void browse_to_curve_node_view(CurveNodeView* nodeView);
	void browse_to_marker_view(MarkerView* markerView);
	void center_in_view(ViewItem* item, enum Qt::AlignmentFlag = Qt::AlignHCenter);
    void keyboard_move_canvas_cursor_to_location(TTimeRef location, qreal sceneY);

	qint64		timeref_scalefactor;

private:
    TSession*           m_session;
    PlayHead*           m_playCursor;
	ClipsViewPort* 		m_clipsViewPort;
	TrackPanelViewPort*	m_tpvp;
	TimeLineViewPort*	m_tlvp;
	QList<TrackView*>	m_audioTrackViews;
	QList<TrackView*>	m_busTrackViews;
    TrackView*          m_sheetMasterOutView;
    TrackView*          m_projectMasterOutView;
    WorkCursor*         m_workCursor;
    TCanvasCursor*      m_canvasCursor;
    QPropertyAnimation* m_canvasCursorMoveAnimation;
    int                 m_sceneHeight{};
    qreal               m_meanTrackHeight{};
    QScrollBar*         m_vScrollBar;
    QScrollBar*         m_hScrollBar;
    bool                m_actOnPlayHead;
    bool                m_viewportReady;

    static QHash<QString, QString> m_cursorsDict;

	// Themeing data
	int	m_trackSeperatingHeight{};
	int	m_trackMinimumHeight{};
	int	m_trackMaximumHeight{};
	int	m_trackTopIndent{};

	void update_tracks_bounding_rect();

    void do_keyboard_canvas_cursor_move(const QPointF &position);

    void collect_item_browser_data(ItemBrowserData& data);
	static void calculate_cursor_dict();


public slots:
	void set_snap_range(int);
	void update_scrollbars();
	void stop_follow_play_head();
	void follow_play_head();
	void set_follow_state(bool state);
	void transport_position_set();

	TCommand* touch();
	TCommand* touch_play_cursor();
	TCommand* center();
	TCommand* scroll_right();
	TCommand* scroll_left();
	TCommand* scroll_up();
	TCommand* scroll_down();
	TCommand* to_upper_context_level();
	TCommand* to_lower_context_level();
	TCommand* browse_to_previous_context_item();
	TCommand* browse_to_next_context_item();
	TCommand* browse_to_context_item_above();
	TCommand* browse_to_context_item_below();
	TCommand* browse_to_time_line();
	TCommand* goto_begin();
	TCommand* goto_end();
	TCommand* add_marker();
	TCommand* add_marker_at_playhead();
	TCommand* add_marker_at_work_cursor();
	TCommand* center_playhead();
	TCommand* toggle_expand_all_tracks(int height = -1);
	TCommand* edit_properties();

private slots:
	void scale_factor_changed();
	void add_new_track_view(Track*);
	void remove_track_view(Track*);
    void hscrollbar_value_changed(int);
	void hscrollbar_action(int);
	void session_vertical_scrollbar_position_changed();
	void session_horizontal_scrollbar_position_changed();
	void context_changed();
	void layout_tracks();
};


inline QHash<QString, QString>* SheetView::cursor_dict()
{
	if (m_cursorsDict.isEmpty()) {
		calculate_cursor_dict();
	}
	return &m_cursorsDict;
}

#endif
