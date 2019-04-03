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

#include "ClipsViewPort.h"
#include "SheetView.h"
#include "ViewPort.h"
#include "PositionIndicator.h"

#include "Debugger.h"

TCanvasCursor::TCanvasCursor(SheetView* )
        : ViewItem(nullptr)
{
	m_textItem = new PositionIndicator(this);
	m_textItem->hide();

	m_ignoreContext = true;
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

	painter->drawPixmap(0, 0, m_pixmap);
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

void TCanvasCursor::set_text( const QString & text, int mseconds)
{
        m_text = text;

	if (m_timer.isActive())
	{
		m_timer.stop();
	}

        if (!m_text.isEmpty()) {
		m_textItem->set_value(m_text);
                m_textItem->show();
		if (mseconds > 0)
		{
			m_timer.start(mseconds);
		}
        } else {
                m_textItem->hide();
        }
}

void TCanvasCursor::set_cursor_shape(QString shape, int alignment)
{
	m_xOffset = m_yOffset = 0;

	if (shape.size() > 1)
	{
		m_pixmap = find_pixmap(shape);
		if (m_pixmap.isNull())
		{
			shape = "";
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

	m_boundingRect = m_pixmap.rect();

	set_pos(m_pos);

	update();
}

void TCanvasCursor::set_pos(QPointF p)
{
	m_pos = p;
	p.setX(p.x() - m_xOffset);
	p.setY(p.y() - m_yOffset);

	setPos(p);

	int textItemX = 40;
	int textItemY = 40;

	ViewPort* vp = static_cast<ViewPort*>(cpointer().get_viewport());
	if (vp && m_textItem->isVisible())
	{
		QPointF textPos(textItemX, textItemY);
        qreal xRightTextItem = vp->mapFromScene(scenePos()).x()  + m_textItem->boundingRect().width() + textItemX;
		int xLeftTextItem = vp->mapFromScene(scenePos()).x() + textItemX;

		int viewPortWidth = vp->width();

		if (xLeftTextItem < 0)
		{
            textPos = mapFromScene(vp->mapToScene(0, int(m_textItem->scenePos().y())));
		}

		if (xRightTextItem > viewPortWidth)
		{
            textPos = mapFromScene(vp->mapToScene(viewPortWidth - int(m_textItem->boundingRect().width()),int(m_textItem->scenePos().y())));
		}

		m_textItem->setPos(textPos);
	}

}

void TCanvasCursor::timer_timeout()
{
	set_text("");
}
