/*
Copyright (C) 2005-2024 Remon Sijrier

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

#include <QPainter>
#include <QFont>
#include <QGraphicsSimpleTextItem>

#include "TAudioClipView.h"
#include "TFrameClock.h"
#include "TLineView.h"
#include "TSheetView.h"
#include "TAudioTrackView.h"
#include "TFadeCurveView.h"
#include "TCurveView.h"
#include "TSplitEntityController.h"
#include "TTrackLaneView.h"

#include "TAudioClip.h"
#include "TInputEventDispatcher.h"
#include "TSheet.h"
#include "TResourcesManager.h"
#include "TProjectManager.h"
#include "TWaveform.h"
#include "TInformUser.h"
#include "TLocation.h"
#include "TThemer.h"
#include "TConfig.h"
#include "TChannelBuffer.h"
#include <TFadeCurve.h>
#include <TCurve.h>
#include "TMainWindow.h"
#include "TAudioPluginChain.h"
#include "Fade.h"
#include "dialogs/AudioClipEditDialog.h"

#include <QElapsedTimer>
#include <QFileDialog>
#include <QApplication>
#include <QLinearGradient>
#include <cmath>

#include "Debugger.h"


TAudioClipView::TAudioClipView(TSheetView* sv, TAudioTrackView* parent, TAudioClip* clip )
    : TViewItem(parent->get_primary_lane_view(), clip)
    , m_tv(parent)
    , m_clip(clip)
    , m_gainCurveView(nullptr)
{
    PENTERCONS;

    setZValue(parent->zValue() + 1);
    setFlags(QGraphicsItem::ItemUsesExtendedStyleOption);

    m_sv = sv;
    m_sheet = m_clip->get_sheet();

    TAudioClipView::load_theme_data();

    m_buildRepaint.setSingleShot(true);
    m_buildRepaint.setInterval(100);
    connect(&m_buildRepaint, &QTimer::timeout, this, [this] { update(); });

    if (m_clip->has_fade_in()) {
        add_new_fade_curve_view(m_clip->get_fade_in());
    }
    if (m_clip->has_fade_out()) {
        add_new_fade_curve_view(m_clip->get_fade_out());
    }

    m_gainCurveView = new TCurveView(m_sv, this, m_clip->audio()->get_fader()->get_curve());
    // CurveViews don't 'get' their start offset, it's only a property for AudioClips.
    // So to be sure the CurveNodeViews start offset get updated as well,
    // we call curveviews calculate_bounding_rect() function!
    m_gainCurveView->set_start_offset(m_clip->get_source_start_location());
    connect(m_gainCurveView, &TCurveView::curveModified, m_sv, &TSheetView::stop_follow_play_head);
    connect(m_gainCurveView, &TCurveView::curveUpdated, this, [this](int startx, int endx) { update(QRectF(startx - 2, 0, endx - startx + 4, m_height)); });

    connect(m_clip->audio(), &TAudioProcessingNode::muteChanged, this, [this](bool a){(void)a; update();});
    connect(m_clip->audio(), &TAudioProcessingNode::muteChanged, this, &TAudioClipView::repaint_clip);
    connect(m_clip, &TAudioClip::edgeMoved, this, &TAudioClipView::repaint_edge);
    connect(m_clip, &TAudioClip::stateChanged, this, &TAudioClipView::clip_state_changed);
    connect(m_clip, &TAudioClip::activeContextChanged, this, &TAudioClipView::active_context_changed);
    connect(m_clip, &TAudioClip::lockChanged, this, [this](){update();});
    connect(m_clip, &TAudioClip::fadeAdded, this, &TAudioClipView::add_new_fade_curve_view);
    connect(m_clip, &TAudioClip::fadeRemoved, this, &TAudioClipView::remove_fade_curve_view);
    connect(m_clip->get_location(), &TLocation::locationChanged, this, &TAudioClipView::position_changed);

    if (m_clip->recording_state() == TAudioClip::RECORDING) {
        start_recording();
        connect(m_clip, &TAudioClip::recordingFinished, this, &TAudioClipView::finish_recording);
    }

    m_splitCursorTransitionLine = new TLineView(this);
    m_splitCursorTransitionLine->set_color(themer()->get_color("AudioClip:contour"));
    m_splitCursorTransitionLine->setVisible(false); // Default state is hidden

}

TAudioClipView::~ TAudioClipView()
{
    PENTERDES;
    // A view that goes while its clip records
    if (m_recordingClock) {
        m_recordingClock->release(this);
    }
}

void TAudioClipView::paint(QPainter* painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    PENTER2;
    Q_UNUSED(widget);

    //        printf("AudioClipView:: %s PAINT :: exposed rect is: x=%f, y=%f,? w=%f, h=%f\n", QS_C(m_clip->get_name()), option->exposedRect.x(), option->exposedRect.y(), option->exposedRect.width(), option->exposedRect.height());

    //	printf("x, w %f, %f\n", option->exposedRect.x(), option->exposedRect.width());
    qreal xstart = option->exposedRect.x();
    qreal pixelcount = option->exposedRect.width()+1;
    if (pixelcount <= 1.0) {
        // apparently this function can be called with no pixelcount to go with
        // so return here nothing to be done
        return;
    }
    // While recording the bounding rect is reserved ahead of the clip's
    // length (see update_recording()); everything is drawn up to the play head
    const QRectF extent = m_clip->recording_state() == TAudioClip::RECORDING
        ? QRectF(0, 0, m_recordingDrawn / m_sv->timeref_scalefactor, m_height)
        : m_boundingRect;

    painter->save();
    painter->setClipRect(extent);
    painter->setRenderHint(QPainter::TextAntialiasing);

    QRectF fillRect = QRectF(xstart, 0.0, pixelcount, qreal(m_height));

    if (m_clip->is_readsource_invalid()) {
        painter->fillRect(fillRect, themer()->get_color("AudioClip:invalidreadsource"));
        draw_clipinfo_area(painter, xstart);
        painter->setPen(themer()->get_color("AudioClip:contour"));
        painter->drawRect(xstart, 0, pixelcount, m_height - 1);
        painter->setPen(Qt::black);
        painter->setFont( themer()->get_font("AudioClip:fontscale:title") );
        painter->drawText(30, 0, 300, m_height, Qt::AlignVCenter, tr("Click to reset AudioFile !"));
        painter->restore();
        return;
    }

    bool mousehover = m_clip->has_active_context() || m_clip->is_moving();

    if (m_drawbackground) {
        if (m_clip->recording_state() == TAudioClip::RECORDING) {
            painter->fillRect(fillRect, m_brushBgRecording);
        } else {
            if (m_clip->is_selected()) {
                if (mousehover) painter->fillRect(fillRect, m_brushBgSelectedHover);
                else            painter->fillRect(fillRect, m_brushBgSelected);
            } else if (m_clip->audio()->is_muted()) {
                if (mousehover) painter->fillRect(fillRect, m_brushBgMutedHover);
                else            painter->fillRect(fillRect, m_brushBgMuted);
            } else {
                if (mousehover) painter->fillRect(fillRect, m_brushBgHover);
                else            painter->fillRect(fillRect, m_brushBg);
            }
        }
    }

    if (m_clip->audio()->is_muted()) {
        m_waveBrush = m_brushFgMuted;
    } else {
        if (mousehover) m_waveBrush = m_brushFgHover;
        else            m_waveBrush = m_brushFg;
    }

    int channels = m_clip->get_channel_count();

    if (channels > 0) {
        if (m_clip->recording_state() != TAudioClip::FINISHING_RECORDING) {
            draw_columns(painter, option->exposedRect.x(), int(pixelcount));
            //                        PROFILE_END("draw peaks");
        }
    }

    // Draw the db lines at 0 and -6 db
    if (m_drawDbGrid) {
        draw_db_lines(painter, xstart, pixelcount);
    }

    draw_clipinfo_area(painter, xstart);

    // Draw the contour
    painter->setPen(themer()->get_color("AudioClip:contour"));
    painter->drawRect(extent.adjusted(0.0, 0, -0.5, -0.5));

    // Paint a pixmap if the clip is locked
    if (m_clip->is_locked()) {
        int center = (int)(m_clip->get_length() / (2 * m_sv->timeref_scalefactor));
        painter->setOpacity(0.4);
        painter->drawPixmap(center - 8,(m_height / 2), QIcon::fromTheme(QIcon::ThemeIcon::SecurityLow).pixmap(m_height / 5, m_height / 5));
    }

    painter->restore();
}

void TAudioClipView::draw_clipinfo_area(QPainter* p, double xstart)
{
    if (xstart > m_clipInfo.deviceIndependentSize().width()) {
        return;
    }

    int margin = 3;
    p->drawPixmap(margin, /*m_height - m_clipInfo.height() -*/ margin, m_clipInfo);
}


void TAudioClipView::draw_db_lines(QPainter* p, qreal xstart, int pixelcount)
{
    p->save();

    int channels = m_clip->get_channel_count();
    bool microView = m_sheet->get_hzoom() < 64 ? true : false;
    int linestartpos = xstart;
    if (xstart < m_lineOffset) linestartpos = m_lineOffset;

    if ((m_mergedView) || (channels == 0)) {
        channels = 1;
    }

    // calculate the height of one channel
    int height = m_height / channels;

    p->setPen(themer()->get_color("AudioClip:db-grid"));
    p->setFont( themer()->get_font("AudioClip:fontscale:dblines") );

    if (m_classicView || microView) { // classicView = non-rectified

        // translate the painter to set the first channel center line to 0
        p->translate(0, height / 2);

        // determine the distance of the db line from the center line
        int zeroDb = 0.9 * height / 2;
        int msixDb = 0.9 * height / 4;

        // draw the lines above and below the center line, then translate
        // the painter to the next channel
        for (int i = 0; i < channels; ++i) {
            p->drawLine(linestartpos, zeroDb, xstart+pixelcount, zeroDb);
            p->drawLine(linestartpos, -zeroDb, xstart+pixelcount, -zeroDb);
            p->drawLine(linestartpos, msixDb, xstart+pixelcount, msixDb);
            p->drawLine(linestartpos, -msixDb + 1, xstart+pixelcount, -msixDb + 1);

            if (xstart < m_lineOffset) {
                p->drawText(0.0, zeroDb - 1 + m_lineVOffset, "  0 dB");
                p->drawText(0.0, -zeroDb + m_lineVOffset, "  0 dB");
                p->drawText(0.0, msixDb + m_lineVOffset, " -6 dB");
                p->drawText(0.0, -msixDb + m_lineVOffset, " -6 dB");
            }


//            p->translate(0, height);
        }
    } else {  // rectified

        // translate the painter to set the first channel base line to 0
        p->translate(0, height);

        // determine the distance of the db line from the center line
        int zeroDb = 0.95 * height;
        int msixDb = 0.95 * height / 2;

        // draw the lines above the center line, then translate
        // the painter to the next channel
        for (int i = 0; i < channels; ++i) {
            p->drawLine(linestartpos, -zeroDb, xstart+pixelcount, -zeroDb);
            p->drawLine(linestartpos, -msixDb + 1, xstart+pixelcount, -msixDb + 1);

            if (xstart < m_lineOffset) {
                p->drawText(0.0, -zeroDb + m_lineVOffset, "  0 dB");
                p->drawText(0.0, -msixDb + m_lineVOffset, " -6 dB");
            }

//            p->translate(0, height);
        }
    }

    p->restore();
}

void TAudioClipView::create_brushes()
{
    /** TODO: The following part is identical to calculations in draw_db_lines(). Move to a central place. **/
    bool microView = m_sheet->get_hzoom() < 64 ? true : false;
    int channels = m_clip->get_channel_count();

    if ((m_mergedView) || (channels == 0)) {
        channels = 1;
    }

    // calculate the height of one channel
    int height = m_height / channels;

    if (m_classicView || microView)
    {
        height *= 0.45;
    } else {
        height *= 0.95;
    }
    /** end of TODO **/

    // create brushes for background states
    m_brushBgRecording = themer()->get_brush("AudioClip:background:recording", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgMuted = themer()->get_brush("AudioClip:background:muted", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgMutedHover = themer()->get_brush("AudioClip:background:muted:mousehover", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgSelected  = themer()->get_brush("AudioClip:background:selected", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgSelectedHover = themer()->get_brush("AudioClip:background:selected:mousehover", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBg = themer()->get_brush("AudioClip:background", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgHover = themer()->get_brush("AudioClip:background:mousehover", QPoint(0, 0), QPoint(0, -m_height));

    // brushes for the wave form
    m_brushFg = themer()->get_brush("AudioClip:wavemacroview:brush", QPoint(0, 0), QPoint(0, -height));
    m_brushFgHover = themer()->get_brush("AudioClip:wavemacroview:brush:hover", QPoint(0, 0), QPoint(0, -height));
    m_brushFgMuted = themer()->get_brush("AudioClip:wavemacroview:brush:muted", QPoint(0, 0), QPoint(0, -height));
    m_brushFgEdit = themer()->get_brush("AudioClip:wavemacroview:brush:curvemode", QPoint(0, 0), QPoint(0, -height));
    m_brushFgEditHover = themer()->get_brush("AudioClip:wavemacroview:brush:curvemode:hover", QPoint(0, 0), QPoint(0, -height));
}

void TAudioClipView::create_clipinfo_string()
{
    PENTER;
    QFont font = themer()->get_font("AudioClip:fontscale:title");
    QFontMetrics fm(font);

    QString clipinfoString = fm.elidedText(m_clip->get_name(), Qt::ElideRight, 400);

    int clipInfoWidth = fm.boundingRect(clipinfoString).width()+2;

    const qreal devicePixelRatio = qApp->devicePixelRatio();
    m_clipInfo = QPixmap(QSize(qRound(clipInfoWidth * devicePixelRatio),
                               qRound(m_infoAreaHeight * devicePixelRatio)));
    m_clipInfo.setDevicePixelRatio(devicePixelRatio);
    m_clipInfo.fill(Qt::transparent);
    QColor textColor = themer()->get_color("AudioClip:text");

    QPainter painter(&m_clipInfo);
    painter.setFont(font);
    painter.setPen(textColor);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.drawText(QRectF(0, 0, clipInfoWidth, m_infoAreaHeight), clipinfoString);
}

void TAudioClipView::build_progressed()
{
    if (!m_buildRepaint.isActive()) {
        m_buildRepaint.start();
    }
}

void TAudioClipView::peak_creation_finished()
{
    m_buildRepaint.stop();
    update();
}

void TAudioClipView::add_new_fade_curve_view( TFadeCurve * fade )
{
    PENTER;
    TFadeCurveView* view = new TFadeCurveView(m_sv, this, fade);
    m_fadeCurveViews.append(view);
    connect(view, &TFadeCurveView::fadeModified, m_sv, &TSheetView::stop_follow_play_head);
    connect(fade, &TFadeCurve::lengthChanged, this, &TAudioClipView::repaint_fade);
    connect(fade, &TFadeCurve::stateChanged, this, &TAudioClipView::repaint_fade);
    connect(fade, &TFadeCurve::bendValueChanged, this, &TAudioClipView::repaint_fade);
    connect(fade, &TFadeCurve::strengthValueChanged, this, &TAudioClipView::repaint_fade);
    connect(fade, &TFadeCurve::modeChanged, this, &TAudioClipView::repaint_fade);
    connect(fade, &TFadeCurve::rasterChanged, this, &TAudioClipView::repaint_fade);
}

void TAudioClipView::remove_fade_curve_view( TFadeCurve * fade )
{
    for (int i = 0; i < m_fadeCurveViews.size(); ++i) {
        TFadeCurveView* view = m_fadeCurveViews.at(i);
        if (view->get_fade() == fade) {
            m_fadeCurveViews.takeAt(i);
            scene()->removeItem(view);
            delete view;
            break;
        }
    }
}

void TAudioClipView::calculate_bounding_rect()
{
    PENTER4;
    prepareGeometryChange();

    m_height = m_parentViewItem->get_height();
    m_boundingRect = QRectF(0, 0, (double(m_clip->get_length().universal_frame()) / m_sv->timeref_scalefactor), m_height);

    uint channelCount = m_clip->get_channel_count();

    // A silent clip readsource on purpose has 0 channels
    // catch and deal with this to avoid a division by 0 below
    if  (channelCount == 0) {
        channelCount = 1;
    }

    if ((m_height / int(channelCount)) < 30) {
        m_classicView = false;
    } else {
        m_classicView = ! config().get_property("Themer", "paintaudiorectified", false).toBool();
    }

    if (m_gainCurveView) {
        m_gainCurveView->updateNodeVisibility(0, (m_clip->get_location_end()-m_clip->get_location_start()) / m_sv->timeref_scalefactor);
    }

    update_start_pos();
    TViewItem::calculate_bounding_rect();
}

void TAudioClipView::repaint_clip()
{
    update(m_boundingRect);
}

void TAudioClipView::repaint_fade()
{
    auto* fade = qobject_cast<TFadeCurve*>(sender());
    for (TFadeCurveView *fadeView : m_fadeCurveViews) {
        if (fadeView->get_fade() == fade) {
            update(QRectF(fadeView->pos().x() - 4, 0, fadeView->boundingRect().width() + 8, m_height));
            return;
        }
    }
}

void TAudioClipView::repaint_edge(bool isLeftEdge)
{
    for (TFadeCurveView *fadeView : m_fadeCurveViews) {
        if ((isLeftEdge && fadeView->get_fade()->get_fade_type() == TFadeCurve::FadeIn) ||
            (!isLeftEdge && fadeView->get_fade()->get_fade_type() == TFadeCurve::FadeOut))
        {
            if (fadeView->get_fade()->get_length() / m_sv->timeref_scalefactor > 0) {
                update(QRectF(fadeView->pos().x() - 4, 0, fadeView->boundingRect().width() + 8, m_height));
            }
            return;
        }
    }
}

void TAudioClipView::update_start_pos()
{
    // 	printf("AudioClipView::update_start_pos()\n");
    setPos((double(m_clip->get_location_start().universal_frame()) / m_sv->timeref_scalefactor), 0);
}

void TAudioClipView::position_changed()
{
    // Update the curveview start offset, only needed for left edge dragging
    // but who cares :)
    // the calculate_bounding_rect() will update AudioClipViews children, so
    // the CurveView and it's nodes get updated as well, no need to set
    // the start offset for those manually!
    m_gainCurveView->set_start_offset(m_clip->get_source_start_location());
    if (!m_tv->get_gain_curve_view()->get_curve()->is_trivial()) {
        // If the track has a non-constant gain curve, we need to invalidate tiles on clip position movement
        this->repaint_clip();
    }
    calculate_bounding_rect();
}

void TAudioClipView::load_theme_data()
{
    m_drawbackground = themer()->get_property("AudioClip:drawbackground", 1).toInt();
    m_infoAreaHeight = themer()->get_property("AudioClip:infoareaheight", 16).toInt();
    m_mimimumheightforinfoarea = themer()->get_property("AudioClip:mimimumheightforinfoarea", 45).toInt();
    m_classicView = ! config().get_property("Themer", "paintaudiorectified", false).toBool();
    m_mergedView = config().get_property("Themer", "paintstereoaudioasmono", false).toBool();
    m_fillwave = themer()->get_property("AudioClip:fillwave", 1).toInt();
    minINFLineColor = themer()->get_color("AudioClip:channelseperator");
    m_paintWithOutline = config().get_property("Themer", "paintwavewithoutline", true).toBool();
    m_drawDbGrid = config().get_property("Themer", "drawdbgrid", false).toBool();
    TAudioClipView::calculate_bounding_rect();

    QFont dblfont = themer()->get_font("AudioClip:fontscale:dblines");
    QFontMetrics fm(dblfont);
    m_lineOffset = fm.horizontalAdvance(" -6 dB ");
    m_lineVOffset = fm.ascent()/2;

    create_brushes();
    create_clipinfo_string();
}


void TAudioClipView::active_context_changed()
{
    if (ied().is_holding()) {
        // TODO: find out if we still need to bail out
        // when holding is active, say for moving a clip with [ D ]
        // or something else?
        //                return;
    }

    if (m_clip->has_active_context()) {
        m_tv->to_front(this);
    }

    update(m_boundingRect);
}


TCommand * TAudioClipView::select_fade_in_shape( )
{
    TMainWindow::instance()->select_fade_in_shape();

    return nullptr;
}

TCommand * TAudioClipView::select_fade_out_shape( )
{
    TMainWindow::instance()->select_fade_out_shape();

    return nullptr;
}

void TAudioClipView::start_recording()
{
    m_recordingDrawn = TTimeRef();
    m_paintedRecordingPos = TTimeRef();
    // The play head's frames (TPlayHeadView), so the clip's end moves with
    // it; a frame paints only the strip that is new
    m_recordingClock = m_sv->get_frame_clock();
    connect(m_recordingClock, &TFrameClock::frame, this, &TAudioClipView::update_recording);
    m_recordingClock->hold(this);
}

void TAudioClipView::finish_recording()
{
    if (m_recordingClock) {
        disconnect(m_recordingClock, &TFrameClock::frame, this, &TAudioClipView::update_recording);
        m_recordingClock->release(this);
        m_recordingClock = nullptr;
    }
    prepareGeometryChange();
    m_boundingRect = QRectF(0, 0, (m_clip->get_length() / m_sv->timeref_scalefactor), m_height);
    m_gainCurveView->calculate_bounding_rect();
    update();
}

void TAudioClipView::update_recording(uint64_t nowNanoseconds)
{
    if (m_clip->recording_state() != TAudioClip::RECORDING) {
        return;
    }

    // Drawn up to the play head, which shows what is heard now: what is
    // played now is recorded there. No further than the clip's length,
    // which counts what was captured past the latency compensation: with it
    // the length stays behind the play head by the capture (and extra)
    // latency, without it runs ahead by the output latency. The waveform
    // follows a little behind, as the writer's steps append its peaks;
    // repaint from where it was painted up to the play head.
    const TTimeRef heard = m_sv->get_sheet()->get_heard_transport_location(nowNanoseconds);
    const TTimeRef start = m_clip->get_location_start();
    TTimeRef drawn = heard > start ? heard - start : TTimeRef();
    if (drawn > m_clip->get_length()) {
        drawn = m_clip->get_length();
    }
    TTimeRef painted = m_clip->get_waveform() ? m_clip->get_waveform()->recorded_length() : drawn;
    if (painted > drawn) {
        painted = drawn;
    }
    const int oldPixel = std::min(int(m_recordingDrawn / m_sv->timeref_scalefactor), int(m_paintedRecordingPos / m_sv->timeref_scalefactor));
    const int newPixel = int(drawn / m_sv->timeref_scalefactor);
    m_recordingDrawn = drawn;
    if (newPixel <= oldPixel) {
        return;
    }

    // A geometry change makes the scene repaint the whole item, so the
    // bounding rect is reserved a minute ahead and only grows now and then;
    // paint() draws within the clip's length
    if (newPixel >= m_boundingRect.width()) {
        prepareGeometryChange();
        const qreal reserve = TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE / qreal(m_sv->timeref_scalefactor);
        m_boundingRect = QRectF(0, 0, newPixel + reserve, m_height);
    }

    // From two pixels back: the grid column at the recording's edge is
    // partial and grows with every append, and a grid column is up to 2
    // device pixels wide (TWaveformColumns::device_columns())
    update(QRectF(oldPixel - 2, 0, newPixel - oldPixel + 3, m_height));
    m_paintedRecordingPos = painted;
}

TCommand * TAudioClipView::set_audio_file()
{
    // FIXME this is not working for 2 reasons
    // 1. clicking on the clip has become another shortcut so this function will never be called
    // 2. get_source_file_name(id, Qstring) and similar added functions broke ResourcesManager

    if (!m_clip->is_readsource_invalid()) {
        return ied().failure();
    }

    // QString fileName;
    // resources_manager()->get_source_file_name(m_clip->get_readsource_id(), fileName);
    // if ( ! fileName.isEmpty() ) {
    //     return ied().failure();
    // }

    // fileName = QFileDialog::getOpenFileName(TMainWindow::instance(),
    //                                                 tr("Reset Audio File for Clip: %1").arg(m_clip->get_name()),
    //                                                 fileName,
    //                                                 tr("Audio files (*.wav *.flac *.ogg *.mp3 *.wv *.w64 *.m4a *.aac)"));

    // if (fileName.isEmpty()) {
    //     tInformUser().information(tr("No file selected!"));
    //     return ied().failure();
    // }

    // ;

    // if (resources_manager()->set_file_for_source(fileName, m_clip->get_readsource_id())) {
    //     return ied().failure();
    // }

    // resources_manager()->set_source_for_clip(m_clip, m_clip->get_readsource_id());


    // // FIXME This is a hack. When a ReadSource didn't have a valid file it wasn't added
    // // to DiskIO in AudioClip::set_sheet(). So when resetting the audiofile this solves it,
    // // but it's not the proper place to do so!!
    // m_clip->set_sheet(m_sheet);

    // tInformUser().information(tr("Succesfully set AudioClip file to %1").arg(fileName));

    return ied().succes();
}

TCommand * TAudioClipView::edit_properties()
{
    auto editdialog = new AudioClipEditDialog(m_clip, TMainWindow::instance());

    editdialog->show();

    return nullptr;
}

void TAudioClipView::clip_state_changed()
{
    repaint_clip();
    update();
}

void TAudioClipView::connect_interaction_controller(TSplitEntityController* controller)
{
    Q_ASSERT(controller);

    m_splitCursorTransitionLine->setVisible(controller->is_holding());

    connect(controller, &TSplitEntityController::isHoldingChanged, this, [this, controller]() {
        m_splitCursorTransitionLine->setVisible(controller->is_holding());
        update();
    });

    connect(controller, &TSplitEntityController::splitLocationChanged, this, [this, controller]() {
        if (m_splitCursorTransitionLine->isVisible()) {
            TTimeRef splitLocation = controller->get_split_location();

            qreal localPixelX = (splitLocation - this->get_clip()->get_location_start()) / m_sv->timeref_scalefactor;
            m_splitCursorTransitionLine->setPos(QPointF(localPixelX, 0));
            update();
        }
    });

    connect(controller, &QObject::destroyed, this, [this]() {
        m_splitCursorTransitionLine->setVisible(false);
        update();
    });
}




void TAudioClipView::fill_gain(const TTimeRef& clipLocalStart, const TTimeRef& columnWidth, int count, float* gain)
{
    const float clipGain = m_clip->audio()->get_gain();
    std::fill(gain, gain + count, clipGain);

    const double width = double(columnWidth.universal_frame());
    TChannelBuffer buffer{nframes_t(count)};

    // The clip's gain curve, in source time
    TCurve* clipCurve = m_clip->audio()->get_fader()->get_curve();
    if (clipCurve) {
        if (clipCurve->is_trivial()) {
            const float trivial = clipCurve->get_trivial_gain();
            for (int i = 0; i < count; ++i) {
                gain[i] *= trivial;
            }
        } else {
            const double x0 = double((m_clip->get_source_start_location() + clipLocalStart).universal_frame());
            clipCurve->get_vector(x0, x0 + width * count, buffer, nframes_t(count));
            for (int i = 0; i < count; ++i) {
                gain[i] *= buffer[i];
            }
        }
    }

    // The track's gain curve, in sheet time
    TCurve* trackCurve = m_tv->get_gain_curve_view()->get_curve();
    if (trackCurve) {
        if (trackCurve->is_trivial()) {
            const float trivial = trackCurve->get_trivial_gain();
            for (int i = 0; i < count; ++i) {
                gain[i] *= trivial;
            }
        } else {
            const double x0 = double((m_clip->get_location_start() + clipLocalStart).universal_frame());
            trackCurve->get_vector(x0, x0 + width * count, buffer, nframes_t(count));
            for (int i = 0; i < count; ++i) {
                gain[i] *= buffer[i];
            }
        }
    }

    // The fades, in time relative to their start; drawing never creates fades
    const double clipLength = double(m_clip->get_length().universal_frame());
    const double start = double(clipLocalStart.universal_frame());
    for (TFadeCurve* fade : {m_clip->get_existing_fade_in(), m_clip->get_existing_fade_out()}) {
        if (!fade) {
            continue;
        }
        const double fadeLength = double(fade->get_length().universal_frame());
        if (fadeLength <= 0.0) {
            continue;
        }
        const double fadeStart = fade->get_fade_type() == TFadeCurve::FadeOut ? clipLength - fadeLength : 0.0;
        // The columns whose start is inside the fade
        const int first = std::max(0, int(std::ceil((fadeStart - start) / width)));
        const int last = std::min(count, int(std::ceil((fadeStart + fadeLength - start) / width)));
        if (last <= first) {
            continue;
        }
        const double x0 = start + first * width - fadeStart;
        fade->get_vector(x0, x0 + width * (last - first), buffer, nframes_t(last - first));
        for (int i = first; i < last; ++i) {
            gain[i] *= buffer[i - first];
        }
    }
}

void TAudioClipView::draw_columns(QPainter* painter, qreal xstart, int pixelcount)
{
    TWaveform* waveform = m_clip->get_waveform();
    if (!waveform || pixelcount <= 0 || m_height <= 0) {
        return;
    }
    const int channels = waveform->channel_count();
    if (channels <= 0) {
        return;
    }

    switch (waveform->state()) {
    case TWaveform::State::Building:
        // The waveform grows as the peaks are built: what is there now,
        // empty columns past it. (A build goes from the start of the source
        // to its end, so the middle of a long source comes when the build
        // gets there.)
        connect(waveform, qOverload<int>(&TWaveform::progress), this, &TAudioClipView::build_progressed, Qt::UniqueConnection);
        connect(waveform, &TWaveform::ready, this, &TAudioClipView::peak_creation_finished, Qt::UniqueConnection);
        break;
    case TWaveform::State::Failed:
        return;
    case TWaveform::State::Ready:
        break;
    }

    QElapsedTimer paintTimer;
    paintTimer.start();

    // One column per device pixel
    const qreal dpr = painter->device()->devicePixelRatioF();
    const int x0 = qFloor(xstart);
    const int logicalWidth = qCeil(xstart + pixelcount) - x0;
    const int columns = qCeil(logicalWidth * dpr);
    const int deviceHeight = qRound(m_height * dpr);
    const double scale = double(m_sv->timeref_scalefactor);
    const TTimeRef columnWidth(qint64(std::llround(scale / dpr)));
    if (columns <= 0 || deviceHeight <= 0 || columnWidth <= qint64(0)) {
        return;
    }
    const TTimeRef sourceStart = m_clip->get_source_start_location();

    // The columns and their gain, in chunks so a rounded column width does
    // not drift from the pixel grid. From one frame per pixel on the
    // columns come from the power-of-two grid in source time
    // (TWaveformColumns::device_columns()); where a grid column reaches
    // outside the clip's source range, exactly.
    const TTimeRef sourceEnd = sourceStart + m_clip->get_length();
    std::vector<TWaveformColumn> data(size_t(columns) * size_t(channels));
    std::vector<TWaveformColumn> chunk;
    std::vector<float> gain(static_cast<size_t>(columns));
    constexpr int Chunk = 256;
    // Columns from the samples show the line through them: an RMS of one or
    // two samples next to it would only darken part of the line
    bool drawRms = true;
    for (int first = 0; first < columns; first += Chunk) {
        const int count = std::min(Chunk, columns - first);
        const TTimeRef clipLocal(qint64(std::llround((x0 + first / dpr) * scale)));
        chunk.resize(size_t(count) * size_t(channels));
        if (waveform->device_columns(sourceStart + clipLocal, columnWidth, count, sourceStart, sourceEnd, chunk.data())
                == TWaveformColumns::Source::Samples) {
            drawRms = false;
        }
        for (int c = 0; c < channels; ++c) {
            std::copy_n(chunk.data() + c * count, count, data.data() + c * columns + first);
        }
        fill_gain(clipLocal, columnWidth, count, gain.data() + first);
    }

    // The wave brush as one premultiplied colour per device row, and darker
    // for the RMS band inside the envelope
    std::vector<quint32> rowColor(size_t(deviceHeight), 0);
    std::vector<quint32> rmsRowColor(size_t(deviceHeight), 0);
    if (m_fillwave) {
        QImage strip(1, deviceHeight, QImage::Format_ARGB32_Premultiplied);
        strip.fill(Qt::transparent);
        strip.setDevicePixelRatio(dpr);
        QPainter stripPainter(&strip);
        stripPainter.fillRect(QRectF(0, 0, 1, m_height), m_waveBrush);
        stripPainter.end();
        for (int y = 0; y < deviceHeight; ++y) {
            const quint32 color = *reinterpret_cast<const quint32*>(strip.constScanLine(y));
            rowColor[size_t(y)] = color;
            // Premultiplied: the colour channels scaled, the alpha kept
            const quint32 red = (((color >> 16) & 0xffu) * 166u) >> 8;
            const quint32 green = (((color >> 8) & 0xffu) * 166u) >> 8;
            const quint32 blue = ((color & 0xffu) * 166u) >> 8;
            rmsRowColor[size_t(y)] = (color & 0xff000000u) | (red << 16) | (green << 8) | blue;
        }
    }

    QImage image(columns, deviceHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const int drawnChannels = m_mergedView ? 1 : channels;
    const int channelHeight = deviceHeight / drawnChannels;
    QPolygonF top;
    QPolygonF bottom;
    top.reserve(columns);
    bottom.reserve(columns);
    QPainter outlinePainter(&image);
    outlinePainter.setPen(QPen(themer()->get_color(m_clip->audio()->is_muted() ? "AudioClip:wavemacroview:outline:muted"
                                                                       : "AudioClip:wavemacroview:outline"), 0));

    for (int drawn = 0; drawn < drawnChannels; ++drawn) {
        const int channelTop = drawn * channelHeight;
        const int channelBottom = channelTop + channelHeight;
        const double center = channelTop + channelHeight / 2.0;
        const double amplitude = m_classicView ? channelHeight * 0.45 : channelHeight * 0.95;
        top.clear();
        bottom.clear();

        for (int i = 0; i < columns; ++i) {
            TWaveformColumn column = data[size_t(i)];
            if (m_mergedView) {
                // The RMS of all channels together: the mean of their squares
                float squares = column.rms * column.rms;
                for (int c = 1; c < channels; ++c) {
                    const TWaveformColumn& other = data[size_t(c) * size_t(columns) + size_t(i)];
                    column.min = std::min(column.min, other.min);
                    column.max = std::max(column.max, other.max);
                    squares += other.rms * other.rms;
                }
                column.rms = std::sqrt(squares / float(channels));
            } else if (drawn > 0) {
                column = data[size_t(drawn) * size_t(columns) + size_t(i)];
            }
            if (column.is_empty()) {
                continue;
            }

            const float g = gain[size_t(i)];
            float low = column.min * g;
            float high = column.max * g;
            if (low > high) {
                std::swap(low, high);
            }
            const float rms = column.rms * std::fabs(g);

            double yTop, yBottom, rmsTop, rmsBottom;
            if (m_classicView) {
                yTop = center - amplitude * high;
                yBottom = center - amplitude * low;
                rmsTop = center - amplitude * rms;
                rmsBottom = center + amplitude * rms;
            } else {
                yTop = channelBottom - amplitude * std::max(std::fabs(low), std::fabs(high));
                yBottom = channelBottom;
                rmsTop = channelBottom - amplitude * rms;
                rmsBottom = channelBottom;
            }
            top.append(QPointF(i + 0.5, yTop));
            bottom.append(QPointF(i + 0.5, yBottom));

            if (!m_fillwave) {
                continue;
            }
            // At least one device pixel, inside the channel; the RMS band
            // (rounded inwards) in its darker colour
            int y0 = std::clamp(int(std::floor(yTop)), channelTop, channelBottom);
            int y1 = std::clamp(std::max(int(std::ceil(yBottom)), y0 + 1), channelTop, channelBottom);
            const int r0 = drawRms ? std::clamp(int(std::ceil(rmsTop)), channelTop, channelBottom) : 0;
            const int r1 = drawRms ? std::clamp(int(std::floor(rmsBottom)), channelTop, channelBottom) : 0;
            for (int y = y0; y < y1; ++y) {
                quint32* pixel = reinterpret_cast<quint32*>(image.scanLine(y)) + i;
                const quint32 source = (y >= r0 && y < r1) ? rmsRowColor[size_t(y)] : rowColor[size_t(y)];
                const quint32 inverse = 255 - (source >> 24);
                const quint32 d = *pixel;
                const quint32 rb = (((d & 0x00ff00ffu) * inverse) >> 8) & 0x00ff00ffu;
                const quint32 ag = (((d >> 8) & 0x00ff00ffu) * inverse) & 0xff00ff00u;
                *pixel = source + rb + ag;
            }
        }

        if (m_paintWithOutline || !m_fillwave) {
            outlinePainter.drawPolyline(top);
            if (m_classicView) {
                outlinePainter.drawPolyline(bottom);
            }
        }
    }
    outlinePainter.end();

    image.setDevicePixelRatio(dpr);
    painter->drawImage(QPointF(x0, 0), image);

    // A slow paint is worth knowing about (zooming feels sluggish then)
    const qint64 microseconds = paintTimer.nsecsElapsed() / 1000;
    if (microseconds > 8000) {
        TLOG_D("peaks", "slow waveform paint: {} columns x {} px in {} ms ({})", columns, deviceHeight,
               microseconds / 1000.0, m_clip->get_name().toStdString());
    }
}
