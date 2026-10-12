/*
Copyright (C) 2005-2009 Remon Sijrier 

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

#ifndef AUDIO_CLIP_VIEW_H
#define AUDIO_CLIP_VIEW_H

#include "TViewItem.h"
#include <defines.h>
#include <QList>
#include <QTimer>
#include <QPolygonF>
#include <QPixmap>

#include "TTimeRef.h"

class TAudioClip;
class TSheet;
class TFadeCurve;
class TCurveView;
class TSheetView;
class TAudioTrackView;
class TFadeCurveView;
class TPeak;
class TLineView;
class TSplitEntityController;

class TAudioClipView : public TViewItem
{
    Q_OBJECT

public:
    TAudioClipView(TSheetView* view, TAudioTrackView* parent, TAudioClip* clip);
    ~TAudioClipView();

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);


    TAudioClip* get_clip() const {return m_clip;}
    TAudioTrackView* get_audio_track_view() const {return m_tv;}
    TCurveView* get_gain_curve_view() const {return m_gainCurveView;}
    int get_height() const {return m_height;}

    void calculate_bounding_rect();
    void load_theme_data();

public:
    void connect_interaction_controller(TSplitEntityController* controller);

private:
    TAudioTrackView* 	m_tv;
    TLineView*          m_splitCursorTransitionLine{nullptr};
    QList<TFadeCurveView*> m_fadeCurveViews;
    TAudioClip* 	m_clip;
    TSheet*		m_sheet;
    TCurveView* 	m_gainCurveView;
    QPolygonF 	m_polygon;
    QPixmap 	m_clipInfo;
    QTimer 		m_recordingTimer;

    float m_progress;
    int m_peakloadingcount{};

    bool m_waitingForPeaks;
    bool m_mergedView{};
    bool m_classicView{};
    bool m_paintWithOutline{};
    bool m_drawDbGrid{};
    int m_height{};
    int m_infoAreaHeight{};
    int m_mimimumheightforinfoarea{};
    int m_lineOffset{};
    int m_lineVOffset{};
    TTimeRef m_oldRecordingPos;

    // theme data
    int m_drawbackground{};
    int m_fillwave{};
    QColor m_backgroundColorTop;
    QColor m_backgroundColorBottom;
    QColor m_backgroundColorMouseHoverTop;
    QColor m_backgroundColorMouseHoverBottom;
    QColor minINFLineColor;
    QBrush m_waveBrush;
    QBrush m_brushBgRecording;
    QBrush m_brushBgMuted;
    QBrush m_brushBgMutedHover;
    QBrush m_brushBgSelected;
    QBrush m_brushBgSelectedHover;
    QBrush m_brushBg;
    QBrush m_brushBgHover;
    QBrush m_brushFg;
    QBrush m_brushFgHover;
    QBrush m_brushFgMuted;
    QBrush m_brushFgEdit;
    QBrush m_brushFgEditHover;

    void create_clipinfo_string();

    void draw_clipinfo_area(QPainter* painter, double xstart);
    void draw_db_lines(QPainter* painter, qreal xstart, int pixelcount);
    void draw_peaks(QPainter* painter, qreal xstart, int pixelcount);
    void draw_tile(QPainter* painter, qreal xstart, int pixelcount);
    void create_brushes();

    friend class TFadeCurveView;

public slots:
    void add_new_fade_curve_view(TFadeCurve* fade);
    void remove_fade_curve_view(TFadeCurve* fade);
    void update_start_pos();
    void position_changed();
    void invalidate_clip_tiles();
    void invalidate_fade_tiles();
    void invalidate_edge_tiles(bool isLeftEdge);
    void invalidate_tiles_range(int startx, int endx);

    TCommand* select_fade_in_shape();
    TCommand* select_fade_out_shape();
    TCommand* set_audio_file();
    TCommand* edit_properties();

private slots:
    void update_progress_info(int progress);
    void peak_creation_finished();
    void start_recording();
    void finish_recording();
    void update_recording();
    void active_context_changed();
    void clip_state_changed();
};

#endif

//eof
