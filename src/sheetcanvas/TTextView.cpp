/*
Copyright (C) 2019 Remon Sijrier

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

#include "TTextView.h"

#include <Themer.h>

const int INDENT = 10;

TTextView::TTextView(ViewItem *parent)
    :ViewItem (parent)
{
    m_boundingRect = QRectF(0, 0, 226, 18);
}

TTextView::~TTextView()
{

}

void TTextView::paint(QPainter *painter, const QStyleOptionGraphicsItem */*option*/, QWidget */*widget*/)
{
    painter->setRenderHint(QPainter::Antialiasing);

    QColor color = themer()->get_color("TrackPanel:header:background");
    painter->setPen(color.darker(180));
    painter->setBrush(color);
    painter->drawRect(m_boundingRect);

    painter->setPen(themer()->get_color("TrackPanel:text"));
    painter->setFont(themer()->get_font("TrackPanel:fontscale:name"));
    painter->translate(10,0);
    painter->drawText(m_boundingRect, Qt::AlignVCenter, m_text);
}

void TTextView::calculate_bounding_rect()
{
    ViewItem::calculate_bounding_rect();
}

void TTextView::setText(const QString &text)
{
    m_text = text;
    update();
}
