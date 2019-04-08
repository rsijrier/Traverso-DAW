/*
Copyright (C) 2011 Remon Sijrier

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

#include "TKnobView.h"

#include "Themer.h"
#include "Track.h"
#include "math.h"

#include <Utils.h>
#include <Mixer.h>

TKnobView::TKnobView(ViewItem *parent)
    : ViewItem(parent, nullptr)
{
    m_minValue = -1.0;
    m_maxValue = 1.0;
	m_totalAngle = 270;
}

void TKnobView::paint( QPainter * painter, const QStyleOptionGraphicsItem * /*option*/, QWidget * widget )
{
	Q_UNUSED(widget);

    int borderWidth = 2;

	painter->setRenderHint(QPainter::Antialiasing);

	QPen pen(QColor(100, 100, 100, 220));
	pen.setWidth(1);
	painter->setPen(pen);
	painter->setBrush(QColor(Qt::transparent));

	int radius;
	double rb, re;
	double rarc;

	rarc = m_angle * M_PI / 180.0;
	double ca = cos(rarc);
	double sa = -sin(rarc);
	radius = m_boundingRect.width() / 2 - borderWidth;
	if (radius < 3) radius = 3;
	int ym = m_boundingRect.y() + radius + borderWidth;
	int xm = m_boundingRect.x() + radius + borderWidth;

	int penWidth = 2;
	pen.setWidth(penWidth);
	painter->setPen(pen);

	rb = qMax(double((radius - penWidth) / 3.0), 0.0);
	re = qMax(double(radius - penWidth), 0.0);

	QPoint center;
	center.setX(m_boundingRect.width() / 2);
	center.setY(m_boundingRect.height() / 2);
	painter->drawEllipse(center, radius, radius);

    pen.setColor(QColor(170, 170, 170, 200));
    painter->setPen(pen);
	painter->drawLine(xm - int(rint(sa * rb)),
			ym - int(rint(ca * rb)),
			xm - int(rint(sa * re)),
			ym - int(rint(ca * re)));

	QFont font = themer()->get_font("TrackPanel:fontscale:name");
	font.setPixelSize(8);
	painter->setFont(font);
    painter->drawText(0, -10, int(m_boundingRect.width()), 10, Qt::AlignHCenter, m_title);
}

void TKnobView::set_width(int width)
{
    m_boundingRect = QRectF(0, 0, width, width);
	load_theme_data();
}

void TKnobView::load_theme_data()
{
	m_gradient2D.setColorAt(0.0, themer()->get_color("PanSlider:-1"));
	m_gradient2D.setColorAt(0.5, themer()->get_color("PanSlider:0"));
	m_gradient2D.setColorAt(1.0, themer()->get_color("PanSlider:1"));
	m_gradient2D.setStart(QPointF(m_boundingRect.width() - 40, 0));
	m_gradient2D.setFinalStop(31, 0);
}


void TKnobView::update_angle()
{
    m_angle = (m_value - 0.5 * (min_value() + max_value()))
			/ (max_value() - min_value()) * m_totalAngle;
	m_nTurns = floor((m_angle + 180.0) / 360.0);
    m_angle = m_angle - m_nTurns * 360.0;
}

void TKnobView::set_value(double value)
 {
    if (value > m_maxValue) {
        value = m_maxValue;
    }
    if (value < m_minValue) {
        value = m_minValue;
    }
    m_value = value;
    update_angle();
    m_parentViewItem->update();
}

void TKnobView::set_min_value(double value)
{
    m_minValue = value;
}

void TKnobView::set_max_value(double value)
{
    m_maxValue = value;
}

void TKnobView::set_title(const QString &title)
{
    m_title = title;
    m_parentViewItem->update();
}

TPanKnobView::TPanKnobView(ViewItem* parent, Track* track)
	: TKnobView(parent)
	, m_track(track)
{
	connect(m_track, SIGNAL(panChanged()), this, SLOT(track_pan_changed()));
    set_title("PAN");
    track_pan_changed();
}

void TPanKnobView::track_pan_changed()
{
    set_value(double(m_track->get_pan()));
}

TCommand* TPanKnobView::pan_left()
{
    m_track->set_pan(m_track->get_pan() - 0.05f);
    return nullptr;
}

TCommand* TPanKnobView::pan_right()
{
    m_track->set_pan(m_track->get_pan() + 0.05f);
    return nullptr;
}


TGainKnobView::TGainKnobView(ViewItem* parent, Track* track)
    : TKnobView(parent)
    , m_track(track)
{
    connect(m_track, SIGNAL(stateChanged()), this, SLOT(track_gain_changed()));
    set_title("GAIN");
    set_min_value(0);
    set_max_value(2);
    track_gain_changed();
}

void TGainKnobView::track_gain_changed()
{
    set_value(double(m_track->get_gain()));
}

