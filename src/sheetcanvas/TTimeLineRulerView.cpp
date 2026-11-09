/*
Copyright (C) 2005-2007 Remon Sijrier 

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

#include "TTimeLineRulerView.h"

#include "TThemer.h"
#include "TSheetView.h"
#include "TTimeLineMarkerView.h"
#include "TTimeLineRulerViewPort.h"
#include "TMainWindow.h"
#include <TSheet.h>
#include <TTimeLineRuler.h>
#include <TTimeLineMarker.h>
#include <TContextPointer.h>
#include <CommandGroup.h>
#include "TInputEventDispatcher.h"
#include <cstdlib>
#include "Debugger.h"
#include "TLocation.h"

#include <QPainter>
#include <QDebug>
#include <QFont>

#define MARKER_SOFT_SELECTION_DISTANCE 50

TTimeLineRulerView::TTimeLineRulerView(TSheetView* view)
    : TViewItem(nullptr, view->get_sheet()->get_timeline_ruler())
    , m_blinkingMarker(nullptr)
{
    PENTERCONS2;

    m_sv = view;
    m_boundingRect = QRectF(0, 0, MAX_CANVAS_WIDTH, TIMELINE_HEIGHT);
    m_timeline = m_sv->get_sheet()->get_timeline_ruler();

    TTimeLineRulerView::load_theme_data();

    // Create MarkerViews for existing markers
    for(TTimeLineMarker* marker : m_timeline->get_markers()) {
        add_new_marker_view(marker);
    }

    // Make connections to the 'core'
    connect(m_timeline, &TTimeLineRuler::markerAdded, this, &TTimeLineRulerView::add_new_marker_view);
    connect(m_timeline, &TTimeLineRuler::markerRemoved, this, &TTimeLineRulerView::remove_marker_view);
    connect(m_timeline, &TTimeLineRuler::activeContextChanged, this, &TTimeLineRulerView::active_context_changed);

    m_hasMouseTracking = true;

    m_zooms[524288 * 640] = "20:00.000";
    m_zooms[262144 * 640] = "10:00.000";
    m_zooms[131072 * 640] = "5:00.000";
    m_zooms[ 65536 * 640] = "2:30.000";
    m_zooms[ 32768 * 640] = "1:00.000";
    m_zooms[ 16384 * 640] = "0:30.000";
    m_zooms[  8192 * 640] = "0:20.000";
    m_zooms[  4096 * 640] = "0:10.000";
    m_zooms[  2048 * 640] = "0:05.000";
    m_zooms[  1024 * 640] = "0:02.000";
    m_zooms[   512 * 640] = "0:01.000";
    m_zooms[   256 * 640] = "0:00.800";
    m_zooms[   128 * 640] = "0:00.400";
    m_zooms[    64 * 640] = "0:00.200";
    m_zooms[    32 * 640] = "0:00.100";
    m_zooms[    16 * 640] = "0:00.050";
    m_zooms[     8 * 640] = "0:00.020";
    m_zooms[     4 * 640] = "0:00.010";
    m_zooms[     2 * 640] = "0:00.005";
    m_zooms[     1 * 640] = "0:00.002";

    setFlags(QGraphicsItem::ItemUsesExtendedStyleOption);
}


TTimeLineRulerView::~TTimeLineRulerView()
{
    PENTERDES;
}


void TTimeLineRulerView::hzoom_changed( )
{
    update();
}

TCommand* TTimeLineRulerView::show_marker_dialog()
{
    return TMainWindow::instance()->show_marker_dialog();
}

void TTimeLineRulerView::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    PENTER3;
    Q_UNUSED(widget);

    painter->save();

    // When the scrollarea moves by a small value, the text
    // can be screwed up, so give it some room, 100 pixels should do!
    qreal xstart = option->exposedRect.x() - 100;
    qreal pixelcount = option->exposedRect.width() + 100;

    if (xstart < 0) {
        xstart = 0;
    }

    painter->setClipRect(m_boundingRect);

    int height = TIMELINE_HEIGHT;

    QColor backgroundColor = themer()->get_color("Timeline:background");
    if (m_timeline->has_active_context()) {
        backgroundColor = backgroundColor.lighter(130);
    }
    painter->fillRect(QRectF(xstart, 0,  pixelcount, height), backgroundColor);

    TTimeRef major;

    if (m_zooms.contains(m_sv->timeref_scalefactor)) {
        major = TTimeRef::msms_to_timeref(m_zooms[m_sv->timeref_scalefactor]);
    } else {
        major = 120 * m_sv->timeref_scalefactor;
    }

    // minor is double so they line up right with the majors,
    // despite not always being an even number of frames
    // @Ben : is still still the same when using TTimeRef based calculations?
    double minor = double(major/double(10));

    TTimeRef firstLocation = (TTimeRef(xstart * m_sv->timeref_scalefactor)/major)*major;
    TTimeRef lastLocation = TTimeRef(xstart * m_sv->timeref_scalefactor + pixelcount * m_sv->timeref_scalefactor);
    int xstartoffset = m_sv->hscrollbar_value();

    painter->setWorldMatrixEnabled(false);

    qint64 count = TTimeRef((lastLocation-firstLocation+major) / minor).universal_frame();

    QList<int> minorTicks, majorTicks;
    QList<TTimeRef> majorTimeRefs;

    // calculate minor tick x values
    for (qint64 i = 0; i < count; i++ ) {
        int x = int((firstLocation + i * minor) / m_sv->timeref_scalefactor) - xstartoffset;
        minorTicks.append(x);
    }

    // calculate major tick x values
    for (TTimeRef location = firstLocation; location < lastLocation; location += major) {
        int x = int(location/m_sv->timeref_scalefactor - xstartoffset);
        majorTicks.append(x);
        majorTimeRefs.append(location);
        // remove duplicates from minor ticks, so major tick is only painted once!
        minorTicks.removeAll(x);
    }

    // Draw minor ticks
    painter->setPen(themer()->get_color("Timeline:minorticks"));
    for(int i=0; i<minorTicks.size(); ++i) {
        int x = minorTicks.at(i);
        painter->drawLine(x, height - 5, x, height - 1);
    }

    painter->setPen(themer()->get_color("Timeline:majorticks"));
    // Draw major ticks
    for (int i=0; i<majorTicks.size(); ++i) {
        int x = majorTicks.at(i);
        painter->drawLine(x, height - TIMELINE_HEIGHT / 2, x, height - 1);
    }

    painter->setPen(themer()->get_color("Timeline:text"));
    painter->setFont( themer()->get_font("Timeline:fontscale:label") );
    // Draw text
    for (int i=0; i<majorTicks.size(); ++i) {
        int x = majorTicks.at(i);
        painter->drawText(x + 4, height - 8, TTimeRef::timeref_to_text(majorTimeRefs.at(i), m_sv->timeref_scalefactor));
    }

    painter->restore();
}

void TTimeLineRulerView::calculate_bounding_rect()
{
    update();
    TViewItem::calculate_bounding_rect();
}


void TTimeLineRulerView::add_new_marker_view(TTimeLineMarker * marker)
{
    TTimeLineMarkerView* view = new TTimeLineMarkerView(marker, m_sv, this);
    view->set_active(false);
    m_markerViews.append(view);
    view->update();
}

void TTimeLineRulerView::remove_marker_view(TTimeLineMarker * marker)
{
    foreach(TTimeLineMarkerView* view, m_markerViews) {
        if (view->get_marker() == marker) {
            m_markerViews.removeAll(view);
            scene()->removeItem(view);
            m_blinkingMarker = nullptr;
            delete view;
            return;
        }
    }
}

TCommand* TTimeLineRulerView::playhead_to_marker()
{
    update_softselected_marker(cpointer().on_first_input_event_scene_pos());

    if (m_blinkingMarker) {
        m_sv->get_sheet()->set_transport_location(m_blinkingMarker->get_marker()->get_location()->get_start());
        return nullptr;
    }

    return ied().did_not_implement();
}

TCommand* TTimeLineRulerView::remove_marker()
{
    if (m_blinkingMarker) {
        TTimeLineMarker* marker = m_blinkingMarker->get_marker();
        if (marker->get_type() == TTimeLineMarker::ENDMARKER && m_markerViews.size() > 1) {
            cpointer().set_canvas_cursor_text(tr("Remove all other markers first."), 2000);
            return ied().failure();
        }
        return m_timeline->remove_marker(marker);
    }

    return nullptr;
}

void TTimeLineRulerView::update_softselected_marker(QPointF pos)
{
    // TODO : pos is scene_pos, but Marker positions are relative
    // to parent, not to scene, but since TimeLineView spans the scene
    // they happen to be the same now. Could change in the future?

    TTimeLineMarkerView* prevMarker = m_blinkingMarker;
    if (m_markerViews.size()) {
        m_blinkingMarker = m_markerViews.first();
    }

    if (! m_blinkingMarker) {
        return;
    }

    int x = int(pos.x());
    int blinkMarkerDist = abs(x - m_blinkingMarker->position());

    foreach(TTimeLineMarkerView* markerView, m_markerViews) {
        int markerDist = abs(x - markerView->position());

        fflush(stdout);
        if (markerDist < blinkMarkerDist) {
            m_blinkingMarker = markerView;
            blinkMarkerDist = abs(x - m_blinkingMarker->position());
        }
    }

    if (blinkMarkerDist > MARKER_SOFT_SELECTION_DISTANCE) {
        m_blinkingMarker = nullptr;
    }

    if (prevMarker && (prevMarker != m_blinkingMarker) ) {
        prevMarker->set_active(false);
        if (m_blinkingMarker) {
            m_blinkingMarker->set_active(true);
        }
    }

    if (!prevMarker && m_blinkingMarker) {
        m_blinkingMarker->set_active(true);
    }
}


void TTimeLineRulerView::active_context_changed()
{
    PENTER;
    if (has_active_context()) {
        if (m_blinkingMarker) {
            m_blinkingMarker->set_active(true);
        }
    } else {
        if (ied().is_holding()) {
            return;
        }

        if (m_blinkingMarker) {
            // TODO add these functions, or something else to
            // let the user know which marker is to be moved!
            m_blinkingMarker->set_active(false);
            m_blinkingMarker = nullptr;
        }

    }

    update();
}


void TTimeLineRulerView::mouse_hover_move_event()
{
    update_softselected_marker(cpointer().scene_pos());
}

TTimeLineMarkerView* TTimeLineRulerView::get_soft_selected_marker_view()
{
    update_softselected_marker(cpointer().on_first_input_event_scene_pos());

    return m_blinkingMarker;
}

TCommand * TTimeLineRulerView::clear_markers()
{
    CommandGroup* group = new CommandGroup(m_timeline, tr("Clear Markers"));

    foreach(TTimeLineMarker *m, m_timeline->get_markers()) {
        group->add_command(m_timeline->remove_marker(m));
    }

    return group;
}

void TTimeLineRulerView::load_theme_data()
{
    // TODO Load pixmap, fonts, colors from themer() !!
    TTimeLineRulerView::calculate_bounding_rect();
}

TTimeLineMarkerView* TTimeLineRulerView::get_marker_view_after(const TTimeRef &location)
{
    // FIXME: only keep this list sorted if markers are added/moved??
    std::sort(m_markerViews.begin(), m_markerViews.end(), [&](TTimeLineMarkerView* left, TTimeLineMarkerView* right) {
        return left->get_marker()->get_location()->get_start() < right->get_marker()->get_location()->get_start();
    });

    foreach(TTimeLineMarkerView* markerView, m_markerViews) {
        if (markerView->get_marker()->get_location()->get_start() > location) {
            return markerView;
        }
    }
    return nullptr;
}

TTimeLineMarkerView* TTimeLineRulerView::get_marker_view_before(const TTimeRef &location)
{
    // FIXME: only keep this list sorted if markers are added/moved??
    std::sort(m_markerViews.begin(), m_markerViews.end(), [&](TTimeLineMarkerView* left, TTimeLineMarkerView* right) {
        return left->get_marker()->get_location()->get_start() < right->get_marker()->get_location()->get_start();
    });

    for (int i=m_markerViews.size() - 1; i>= 0; --i) {
        TTimeLineMarkerView* markerView = m_markerViews.at(i);
        if (markerView->get_marker()->get_location()->get_start() < location) {
            return markerView;
        }
    }
    
    return nullptr;
}
