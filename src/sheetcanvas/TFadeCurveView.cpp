/*
Copyright (C) 2006 Remon Sijrier

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

$Id: FadeCurveView.cpp,v 1.2 2008/05/24 17:27:49 r_sijrier Exp $
*/

#include "TFadeCurveView.h"
#include "TCurvePolyline.h"

#include <QPainter>

#include "TFadeCurve.h"
#include "TAudioClipView.h"
#include "TSheetView.h"
#include "TMainWindow.h"
#include <TThemer.h>
#include <Fade.h>
#include "TInputEventDispatcher.h"

#include <TSheet.h>

#include <Debugger.h>

static const int DOT_SIZE		= 6;
const char* DOT_COLOR{"#78817B"};

TFadeCurveView::TFadeCurveView(TSheetView* sv, TAudioClipView* parent, TFadeCurve * fadeCurve )
    : TViewItem(parent, fadeCurve)
    , m_fadeCurve(fadeCurve)
{
    PENTERCONS;
    m_sv = sv;
    m_audioClip = parent->get_clip();
    m_holdactive = false;

    Q_ASSERT(m_fadeCurve);

    TFadeCurveView::load_theme_data();

    setFlags(QGraphicsItem::ItemUsesExtendedStyleOption);

    connect(m_fadeCurve, &TFadeCurve::stateChanged, this, &TFadeCurveView::state_changed);
    connect(m_fadeCurve, &TFadeCurve::lengthChanged, this, &TFadeCurveView::state_changed);
}


TFadeCurveView::~ TFadeCurveView( )
{
    PENTERDES;
}


void TFadeCurveView::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(widget);


    int pixelcount = int(option->exposedRect.width());

    if (pixelcount == 0) {
        return;
    }

    qreal xstart = option->exposedRect.x();
    if (xstart > 0) {
            xstart -= 1;
            pixelcount += 1;
    }
    pixelcount += 1;
    qreal vector_start = xstart;
    qreal height = m_boundingRect.height();

    // The fade is in time units, one pixel is timeref_scalefactor universal frames
    const double scale = double(m_sv->timeref_scalefactor);
    const qreal range = m_fadeCurve->get_length() / scale;

    if (m_fadeCurve->get_fade_type() == TFadeCurve::FadeOut && range > m_parentViewItem->boundingRect().width()) {
        vector_start += range - m_parentViewItem->boundingRect().width();
    }

    // One column per pixel, reduced to half a device pixel: filling costs
    // time per point (measured 2026-10-15 with the curve paint benchmark of
    // c3c1861a, since removed)
    std::vector<TCurveColumn> columns(static_cast<size_t>(pixelcount));
    m_fadeCurve->get_columns(vector_start * scale, scale, pixelcount, columns.data());
    const qreal tolerance = 0.5 / painter->device()->devicePixelRatioF();
    QPolygonF polygon = curve_polyline(columns.data(), pixelcount, xstart + 1, height, tolerance);


    painter->save();
    painter->setClipRect(m_boundingRect.intersected(m_parentViewItem->boundingRect()));
    painter->setRenderHint(QPainter::Antialiasing);

    QPainterPath path;

    path.addPolygon(polygon);
    path.lineTo(xstart + 1 + pixelcount, 0);
    path.lineTo(xstart + 1, 0);
    path.closeSubpath();

    painter->setPen(Qt::NoPen);

    QColor color = m_fadeCurve->is_bypassed() ?
            themer()->get_color("Fade:bypassed") :
            themer()->get_color("Fade:default");

    if (has_active_context()) {
        color.setAlpha(color.alpha() + 10);
    }

    painter->setBrush(color);
    painter->drawPath(path);


    if (m_holdactive) {
        // Calculate and draw control points
        qreal h = m_boundingRect.height() - 1;
        qreal w = m_boundingRect.width() - 1;
        QList<QPointF> points = m_fadeCurve->get_control_points();
        QPointF p1((points.at(1).x() * w + 0.5), h - (points.at(1).y() * h + 0.5));
        QPointF p2(w - ((1.0 - points.at(2).x()) * w + 0.5), ((1.0 - points.at(2).y()) * h + 0.5));

        painter->setPen(QColor(DOT_COLOR));
        painter->setBrush(QColor(DOT_COLOR));

        if (m_fadeCurve->get_fade_type() == TFadeCurve::FadeOut) {
            p1.setX(w - int((1 - points.at(2).x()) * w + 0.5));
            p1.setY(h - int((1 - points.at(2).y()) * h + 0.5));
            p2.setX(int((points.at(1).x()) * w + 0.5));
            p2.setY(int((points.at(1).y()) * h + 0.5));
            painter->drawLine(QPointF(w, h), QPointF(p1.x(), p1.y()));
            painter->drawLine(QPointF(0, 0), QPointF(p2.x(), p2.y()));
        } else {
            painter->drawLine(QPointF(0, h), QPointF(p1.x(), p1.y()));
            painter->drawLine(QPointF(w, 0), QPointF(p2.x(), p2.y()));
        }

        painter->drawEllipse(QPointF(p1.x() - DOT_SIZE/2, p1.y() - DOT_SIZE/2), DOT_SIZE, DOT_SIZE);
        painter->drawEllipse(QPointF(p2.x() - DOT_SIZE/2, p2.y() - DOT_SIZE/2), DOT_SIZE, DOT_SIZE);
    }

    painter->restore();
}

void TFadeCurveView::calculate_bounding_rect()
{
    prepareGeometryChange();

    double range = m_fadeCurve->get_length() / double(m_sv->timeref_scalefactor);
    m_boundingRect = QRectF( 0, 0, range, m_parentViewItem->get_height() );

    if (m_fadeCurve->get_fade_type() == TFadeCurve::FadeOut) {
        qreal diff = 0;
        if (m_boundingRect.width() > m_parentViewItem->boundingRect().width()) {
            diff = m_boundingRect.width() - m_parentViewItem->boundingRect().width();
        }
        setPos(m_parentViewItem->boundingRect().width() - m_boundingRect.width() + diff, 0);
    } else {
        setPos(0, 0);
    }
}


void TFadeCurveView::state_changed( )
{
    PENTER;
    prepareGeometryChange();
    calculate_bounding_rect();
    update();

    emit fadeModified();
}

TCommand* TFadeCurveView::select_fade_shape()
{
    if (m_fadeCurve->get_fade_type() == TFadeCurve::FadeIn) {
        TMainWindow::instance()->select_fade_in_shape();
    }
    else {
        TMainWindow::instance()->select_fade_out_shape();
    }
    return nullptr;
}

void TFadeCurveView::set_holding(bool hold)
{
    m_holdactive = hold;
    update(m_boundingRect);
}


void TFadeCurveView::load_theme_data()
{
    TFadeCurveView::calculate_bounding_rect();
}

