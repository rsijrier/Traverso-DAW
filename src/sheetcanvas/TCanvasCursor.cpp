/*
Copyright (C) 2010 Remon Sijrier

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


#include "TCanvasCursor.h"

#include "SheetView.h"
#include "ViewPort.h"
#include "PositionIndicator.h"

#include "Debugger.h"

TCanvasCursor::TCanvasCursor(SheetView* )
    : ViewItem(nullptr)
{
    m_positionIndicator = new PositionIndicator(this);
    m_positionIndicator->hide();

    set_ignore_context(true);
    m_shape = "";
    m_xOffset = m_yOffset = 0.0;

    setZValue(20000);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(timer_timeout()));
}

TCanvasCursor::~TCanvasCursor( )
{
}

void TCanvasCursor::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
    Q_UNUSED(widget);
    Q_UNUSED(option);

    painter->drawPixmap(int(-m_xOffset), int(-m_yOffset), m_pixmap);
}

void TCanvasCursor::create_cursor_pixmap(const QString &shape)
{
    int width = 13;
    int height = 20;
    int bottom = height + height / 2 - 2;
    m_pixmap = QPixmap(width + 2, bottom);
    m_pixmap.fill(Qt::transparent);
    QPainter painter(&m_pixmap);
    QPainterPath path;

    qreal halfWidth = width / 2;
    QPointF endPoint(halfWidth + 1, 1);
    QPointF c1(1, height);
    QPointF c2(width + 1, height);
    path.moveTo(endPoint);
    path.quadTo(QPointF(-1, height * 0.75), c1);
    path.quadTo(QPointF(halfWidth + 1, bottom), c2);
    path.quadTo(QPointF(width + 3, height * 0.75), endPoint);
    QLinearGradient gradient;
    int graycolor = 180;
    int transparanty = 230;
    QColor gray(graycolor, graycolor, graycolor, transparanty);
    QColor black(0, 0, 0, transparanty);
    gradient.setColorAt(0.0, gray);
    gradient.setColorAt(1.0, black);
    gradient.setStart(0, 0);
    gradient.setFinalStop(0, -height);
    gradient.setSpread(QGradient::ReflectSpread);

    painter.setBrush(gradient);
    int white = 230;
    painter.setPen(QColor(white, white, white));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.drawPath(path);
    QColor color (Qt::yellow);
    painter.setPen(color);
    QFont font;
    font.setPointSizeF(8);
    font.setKerning(false);
    painter.setFont(font);
    QRectF textRect(0, 11, width + 2, height - 11);
    painter.drawText(textRect, Qt::AlignCenter, shape);
}

void TCanvasCursor::set_text(const QString & first, int mseconds)
{
    m_primaryText = first;

    if (m_timer.isActive()) {
        m_timer.stop();
    }

    if (m_primaryText.isEmpty()) {
        m_positionIndicator->hide();
        return;
    }

    m_positionIndicator->set_text(m_primaryText);
    update_textitem_pos();
    m_positionIndicator->show();
    if (mseconds > 0){
        m_timer.start(mseconds);
    }
}


void TCanvasCursor::set_cursor_shape(const QString &shape, int alignment)
{
    PENTER;

    if (m_shape == shape) {
        return;
    }

    m_shape = shape;
    m_xOffset = m_yOffset = 0;

    if (shape.size() > 1)
    {
        m_pixmap = find_pixmap(shape);
        if (m_pixmap.isNull())
        {
            m_shape = "";
        }
    }

    if (shape.size() <= 1)
    {
        create_cursor_pixmap(shape);
        set_text("");
    }

    if (alignment & Qt::AlignTop)
    {
        m_yOffset = 0;
    }

    if (alignment & Qt::AlignHCenter)
    {
        m_xOffset = qreal(m_pixmap.width()) / 2;
    }

    if (alignment & Qt::AlignVCenter)
    {
        m_yOffset = qreal(m_pixmap.height() / 2);
    }

    prepareGeometryChange();
    m_boundingRect = QRect(-m_pixmap.width(), -m_pixmap.height(), 2 * m_pixmap.width(), 2*m_pixmap.height());

    update_textitem_pos();
}

void TCanvasCursor::update_textitem_pos()
{
    ViewPort* vp = static_cast<ViewPort*>(cpointer().get_viewport());
    if (!vp || !m_positionIndicator->isVisible())
    {
        return;
    }

    qreal textItemX = 25;
    int textItemY = 25;

    QPointF textPos(textItemX, textItemY);

    qreal xRightTextItem = vp->mapFromScene(scenePos()).x()  + m_positionIndicator->boundingRect().width() + textItemX;
    qreal xLeftTextItem = vp->mapFromScene(scenePos()).x() + textItemX;

    int viewPortWidth = vp->width();

    if (xLeftTextItem < 0)
    {
        textItemX = mapFromScene(vp->mapToScene(0, int(m_positionIndicator->scenePos().y()))).x();
    }

    if (xRightTextItem > viewPortWidth)
    {
        textItemX = mapFromScene(vp->mapToScene(viewPortWidth - int(m_positionIndicator->boundingRect().width()),int(m_positionIndicator->scenePos().y()))).x();
    }

    textPos.setX(textItemX);

    m_positionIndicator->setPos(textPos);
}

void TCanvasCursor::timer_timeout()
{
    set_text("");
}
