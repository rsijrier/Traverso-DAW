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

#ifndef TEDITCURSOR_H
#define TEDITCURSOR_H

#include "ViewItem.h"

#include <QTimer>

class SheetView;
class PositionIndicator;

class TCanvasCursor : public ViewItem
{
    Q_OBJECT
    Q_PROPERTY(QPointF position READ get_pos WRITE set_pos)

public:
    TCanvasCursor(SheetView* );
    ~TCanvasCursor();

    enum {
        First,
        Second
    };

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

    void set_text(const QString& first, int mseconds=-1);
    void set_cursor_shape(const QString& shape, int alignment);

private:
    PositionIndicator*      m_positionIndicator;

    QString         m_shape;
    qreal			m_xOffset;
    qreal			m_yOffset;
    QPixmap			m_pixmap;
    QString			m_primaryText;
    QTimer			m_timer;

    void create_cursor_pixmap(const QString& shape);
    void update_textitem_pos();

public slots:
    QPointF get_pos() const {return pos();}
    void set_pos(const QPointF& position) {
        setPos(position);
        update_textitem_pos();
    }

private slots:
    void timer_timeout();
};

#endif // TEDITCURSOR_H
