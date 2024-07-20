/*
    Copyright (C) 2008 Remon Sijrier
    Copyright (C) 2005-2006 Nicola Doebelin

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


#include "CorrelationMeterWidget.h"

#include <PluginChain.h>
#include <CorrelationMeter.h>
#include "TCommand.h"
#include <Sheet.h>
#include <TBusTrack.h>
#include <Themer.h>
#include "TConfig.h"
#include <cmath> // used for fabs

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

//static const float SMOOTH_SHIFT = 0.05;

CorrelationMeterWidget::CorrelationMeterWidget(QWidget* parent)
    : MeterWidget(parent, new CorrelationMeterView(this))
{
    PENTERCONS;
}


CorrelationMeterView::CorrelationMeterView(CorrelationMeterWidget* widget)
    : MeterView(widget)
{
    m_meter = new CorrelationMeter();
    m_meter->init();

    load_theme_data();
    load_configuration();
    connect(themer(), SIGNAL(themeLoaded()), this, SLOT(load_theme_data()), Qt::QueuedConnection);
}

void CorrelationMeterView::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    PENTER3;

    QFontMetrics fm(themer()->get_font("CorrelationMeter:fontscale:label"));

    qreal r = 90.0 / range;

    painter->fillRect(0, 0, m_widget->width(), m_widget->height(), m_bgBrush);

    int lend = int(0.5*m_widget->width() - (-coeff + 1.0) * r * m_widget->width() * (1.0 - fabs(direction)));
    int rend = int(0.5*m_widget->width() + (-coeff + 1.0) * r * m_widget->width() * (1.0 - fabs(direction)));

    int wdt = abs(lend - rend);
    int centerOffset = int(m_widget->width() * r * direction);

    int lpos = int((0.50 - r) * m_widget->width());
    int cpos = m_widget->width()/2;
    int rpos = int((0.50 + r) * m_widget->width());

    qreal vtop = qreal(fm.height() + 1);
    qreal vmid = qreal(fm.height() + 1 + (m_widget->height() - fm.height() + 1)/2);
    qreal vbot = qreal(m_widget->height());

    gradPhase.setStart(QPointF(lend + centerOffset, 0.0));
    gradPhase.setFinalStop(QPointF(rend + centerOffset, 0.0));

    QPen pen(themer()->get_color("CorrelationMeter:centerline"));
    painter->setBrush(QBrush(gradPhase));
    painter->setPen(pen);
    painter->setRenderHint(QPainter::Antialiasing);

    // using cubic splines to draw curved lines is resource hungry. For now I implemented two solutions,
    // one using splines, the other using straight lines, which performs better. Use this bool to switch
    // between the two types for now. (ND)
    bool useCubicSpline = true;

    QPainterPath poly;
    if (useCubicSpline) {
        poly.moveTo(QPointF(lend + centerOffset, vmid));
        poly.cubicTo(QPointF(cpos + centerOffset - wdt/2, vmid), QPointF(cpos + centerOffset,(vmid + vtop)/2), QPointF(cpos + centerOffset, vtop));
        poly.cubicTo(QPointF(cpos + centerOffset,(vmid + vtop)/2), QPointF(cpos + centerOffset + wdt/2, vmid), QPointF(rend + centerOffset, vmid));
        poly.cubicTo(QPointF(cpos + centerOffset + wdt/2, vmid), QPointF(cpos + centerOffset,(vmid + vbot)/2), QPointF(cpos + centerOffset, vbot));
        poly.cubicTo(QPointF(cpos + centerOffset,(vmid + vbot)/2), QPointF(cpos + centerOffset - wdt/2, vmid), QPointF(lend + centerOffset, vmid));
    } else {
        poly.moveTo(QPointF(lend + centerOffset, vmid));
        poly.lineTo(QPointF(cpos + centerOffset, vtop));
        poly.lineTo(QPointF(rend + centerOffset, vmid));
        poly.lineTo(QPointF(cpos + centerOffset, vbot));
        poly.lineTo(QPointF(lend + centerOffset, vmid));
    }

    painter->drawPath(poly);

    // center line
    pen.setWidth(3);
    painter->setPen(pen);
    painter->drawLine(cpos + centerOffset, 0, cpos + centerOffset, m_widget->height());

    painter->setPen(themer()->get_color("CorrelationMeter:grid"));
    painter->drawLine(cpos, 0, cpos, m_widget->height());
    if (range > 180) {
        painter->drawLine(lpos, 0, lpos, m_widget->height());
        painter->drawLine(rpos, 0, rpos, m_widget->height());
    }

    painter->setFont(themer()->get_font("CorrelationMeter:fontscale:label"));

    if (m_widget->height() < 2*fm.height()) {
        return;
    }

    painter->setPen(themer()->get_color("CorrelationMeter:text"));
    painter->fillRect(0, 0, m_widget->width(), fm.height() + 1, themer()->get_color("CorrelationMeter:margin"));
    painter->drawText(cpos - fm.horizontalAdvance("C")/2, fm.ascent() + 1, "C");

    if (range == 180) {
        painter->drawText(1, fm.ascent() + 1, "L");
        painter->drawText(m_widget->width() - fm.horizontalAdvance("R") - 1, fm.ascent() + 1, "R");
    } else {
        painter->drawText(lpos - fm.horizontalAdvance("L")/2, fm.ascent() + 1, "L");
        painter->drawText(rpos - fm.horizontalAdvance("R")/2, fm.ascent() + 1, "R");
    }
}

void CorrelationMeterView::update_data()
{
    if (!m_meter) {
        return;
    }

    // MultiMeter::get_data() will assign it's data to coef and direction
    // if no data was available, return, so we _only_ update the widget when
    // it needs to be!
    if ((qobject_cast<CorrelationMeter*>(m_meter))->get_data(coeff, direction) == 0) {
        return;
    }

    update();
}

TCommand* CorrelationMeterView::set_mode()
{
    switch (range) {
    case 180 : range = 240; break;
    case 240 : range = 360; break;
    case 360 : range = 180; break;
    }
    update();
    save_configuration();
    return nullptr;
}

void CorrelationMeterView::save_configuration()
{
    config().set_property("CorrelationMeter", "Range", range);
}

void CorrelationMeterView::load_configuration()
{
    range = config().get_property("CorrelationMeter", "Range", 360).toInt();
}

void CorrelationMeterView::load_theme_data()
{
    gradPhase = themer()->get_gradient("CorrelationMeter:foreground");

    /** TODO: When I replace QPoint(0, 100) with QPoint(0, m_widget->height()) I get a segmentation fault. WHY??? **/
    m_bgBrush = themer()->get_brush("CorrelationMeter:background", QPoint(0, 0), QPoint(0, 100));
}

//eof
