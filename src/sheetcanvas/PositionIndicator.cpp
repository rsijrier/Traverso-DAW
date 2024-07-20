/*
    Copyright (C) 2007 Remon Sijrier 
 
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

#include "PositionIndicator.h"

#include <QColor>
#include "Utils.h"
#include "Themer.h"

PositionIndicator::PositionIndicator(ViewItem* parentView)
    : ViewItem(parentView, nullptr)
{
    set_ignore_context(true);
	setZValue(200);
}

void PositionIndicator::paint(QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget)
{
	Q_UNUSED(option);
	Q_UNUSED(widget);
	
    painter->save();
	painter->drawPixmap(0, 0, m_background);
	painter->setPen(Qt::black);
	painter->setFont(themer()->get_font("TrackPanel:fontscale:name"));
    painter->drawText(10, 16, m_primaryText);
    painter->restore();
}

void PositionIndicator::calculate_bounding_rect()
{
	prepareGeometryChange();
    QFontMetrics fm(themer()->get_font("TrackPanel:fontscale:name"));
    QRect primaryTextRect = fm.boundingRect(m_primaryText);
    int height = 0;
    int verticalspacing = 8;
    if (!m_primaryText.isEmpty()) {
        height += primaryTextRect.height() + verticalspacing;
    }

    m_boundingRect = QRectF(0, 0, primaryTextRect.width() + 20, height);

    m_background = QPixmap(int(m_boundingRect.width()), int(m_boundingRect.height()));
	m_background.fill(QColor(Qt::transparent));
	
	QPainter painter(&m_background);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setBrush(QColor(255, 255, 255, 200));
	painter.setPen(Qt::NoPen);
    qreal rounding = m_boundingRect.height() / 4;
	painter.drawRoundedRect(m_boundingRect, rounding, rounding);
}

void PositionIndicator::set_text(const QString& primary)
{
    m_primaryText = primary;
    calculate_bounding_rect();
	update();
}

