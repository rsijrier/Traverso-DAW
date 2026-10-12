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

#ifndef TSHEET_VIEW_H
#define TSHEET_VIEW_H

#include "TViewItem.h"
#include "TViewPortInterface.h"
#include <QPropertyAnimation>
#include "TTimeRef.h"

class TAudioClip;
class TSheet;
class TAudioClipView;
class TAudioTrackView;
class TClipsViewPort;
class TCurveView;
class TCurveNodeView;
class TTimeLineMarkerView;
class TTrackPanelViewPort;
class TTimeLineRulerViewPort;
class TTimeLineRulerView;
class TTrackView;
class TSession;
class TSheetWidget;
class TAudioTrackView;
class TTrack;
class TPlayHeadView;
class TWorkCursorView;
class TCanvasCursor;
class TCurve;
class TContextPointer;

struct ItemBrowserData {
	ItemBrowserData() {
		acv = 0;
		atv = 0;
		tv = 0;
		curveView = 0;
		markerView = 0;
		timeLineView = 0;
	}

	TTimeLineRulerView* timeLineView;
	TTimeLineMarkerView* markerView;
	TTrackView* tv;
	TAudioTrackView* atv;
	TAudioClipView* acv;
	TCurveView* curveView;
	QString currentContext;
};

class TSheetView : public TViewItem
{
    Q_OBJECT

public :

    TSheetView(TSheetWidget* sheetwidget,
               TClipsViewPort* viewPort,
               TTrackPanelViewPort* tpvp,
               TTimeLineRulerViewPort* tlvp,
               TSession* sheet);
    ~TSheetView();

    void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) {}
    QRectF boundingRect() const {return QRectF();}

    TSession* get_sheet() const {return m_session;}
    TTrackPanelViewPort* get_trackpanel_view_port() const;
    TClipsViewPort* get_clips_viewport() const;
    TTimeLineRulerViewPort* get_timeline_viewport() const;
    TPlayHeadView* get_play_cursor() const {return m_playCursor;}
    TWorkCursorView* get_work_cursor() const {return m_workCursor;}

    TAudioTrackView* get_audio_trackview_at_scene_pos(QPointF point);
    TTrackView* get_trackview_at_scene_pos(QPointF point);
    QList<TTrackView*> get_track_views() const;
    QList<TTrackView*> get_audio_track_views() const {return m_audioTrackViews;}
    QList<TTrackView*> get_bus_track_views() const {return m_busTrackViews;}
    int get_track_height(TTrack* track) const;
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
    void move_trackview_up(TTrackView* trackView);
    void move_trackview_down(TTrackView* trackView);
    void to_top(TTrackView* trackView);
    void to_bottom(TTrackView* trackView);
    void set_track_height(TTrackView* view, int newheight);
    void set_hscrollbar_value(int value);
    void set_vscrollbar_value(int value);

    void set_canvas_cursor_type(TContextPointer::CursorType cursorShape);
    void set_edit_cursor_text(const QString& text, int mseconds=-1);
    void set_canvas_cursor_pos(QPointF pos, TViewPortInterface::CursorMoveReason reason);
    void set_canvas_cursor_data(const QVariant& data);
    void mouse_hover_move_event();


    void browse_to_track(TTrack* track);
    void browse_to_audio_clip_view(TAudioClipView* acv);
    void browse_to_curve_view(TCurveView* curveView);
    void browse_to_curve_node_view(TCurveNodeView* nodeView);
    void browse_to_marker_view(TTimeLineMarkerView* markerView);
    void center_in_view(TViewItem* item, enum Qt::AlignmentFlag = Qt::AlignHCenter);
    void set_canvas_cursor_location(const TTimeRef &location, qreal sceneY);

    qint64		timeref_scalefactor;

private:
    TSession*           m_session;
    TPlayHeadView*           m_playCursor;
    TClipsViewPort* 		m_clipsViewPort;
    TTrackPanelViewPort*	m_tpvp;
    TTimeLineRulerViewPort*	m_tlvp;
    QList<TTrackView*>	m_audioTrackViews;
    QList<TTrackView*>	m_busTrackViews;
    TTrackView*          m_sheetMasterOutView;
    TTrackView*          m_projectMasterOutView;
    TWorkCursorView*         m_workCursor;
    TCanvasCursor*      m_canvasCursor;
    QPropertyAnimation* m_canvasCursorMoveAnimation;
    TContextPointer*    m_contextPointer{nullptr};

    int                 m_sceneHeight{};
    qreal               m_meanTrackHeight{};
    QScrollBar*         m_vScrollBar;
    QScrollBar*         m_hScrollBar;
    bool                m_actOnPlayHead;
    bool                m_viewportReady;
    bool                m_addRemoveBatchActive{false};

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
	TCommand* scroll_right_by(int delta);
	TCommand* scroll_left_by(int delta);
	TCommand* scroll_up_by(int delta);
	TCommand* scroll_down_by(int delta);
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
    TCommand* center_playhead();
    TCommand* toggle_expand_all_tracks(int height = -1);
    TCommand* edit_properties();

private slots:
    void scale_factor_changed();
    void add_new_track_view(TTrack*);
    void remove_track_view(TTrack*);
    void hscrollbar_value_changed(int);
    void hscrollbar_action(int);
    void session_vertical_scrollbar_position_changed();
    void session_horizontal_scrollbar_position_changed();
    void context_changed();
    void layout_tracks();
};


inline QHash<QString, QString>* TSheetView::cursor_dict()
{
	if (m_cursorsDict.isEmpty()) {
		calculate_cursor_dict();
	}
	return &m_cursorsDict;
}

#endif
