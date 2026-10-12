/*
Copyright (C) 2024 Remon Sijrier

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

#ifndef TVIEWPORTINTERFACE_H
#define TVIEWPORTINTERFACE_H

#include <QPointF>
#include <QList>
#include <QVariant>
#include <QFlags>
#include "TTimeRef.h"
#include "TContextPointer.h"
#include "TInteractionCursors.h"

class TContextItem;

class TViewPortInterface
{
public:
    TViewPortInterface() = default;
    virtual ~TViewPortInterface() = default;

    enum CursorMoveReason {
        UNDEFINED,
        MOUSE_MOVE_EVENT,
        KEYBOARD_NAVIGATION,
        CURSOR_SHAPE_CHANGE
    };

    enum ScrollMode {
        None        = 0x0,
        Horizontal  = 0x1,
        Vertical    = 0x2,
        Omni        = Horizontal | Vertical
    };
    Q_DECLARE_FLAGS(ScrollModes, ScrollMode)

    virtual QPointF map_to_scene(const QPoint& pos) const = 0;
    virtual QPoint map_to_global(const QPoint& pos) const = 0;
    virtual void set_canvas_cursor_type(TContextPointer::CursorType) = 0;
    virtual void set_canvas_cursor_controller_type(TraversoDAW::CursorType) = 0;
    virtual void set_canvas_cursor_text(const QString& text, int mseconds) = 0;
    virtual void set_canvas_cursor_pos(QPointF pos, CursorMoveReason reason = UNDEFINED) = 0;
    virtual void set_canvas_cursor_data(const QVariant& data) = 0;
    virtual void set_canvas_cursor_location(const TTimeRef &location, qreal sceneY) = 0;
    virtual void detect_items_below_cursor() = 0;

    virtual void grab_mouse() = 0;
    virtual void release_mouse() = 0;

    virtual void scroll_horizontal(int pixels) = 0;
    virtual void scroll_vertical(int pixels) = 0;
    virtual int viewport_width() const = 0;
    virtual int viewport_height() const = 0;

    virtual void refresh() = 0;

    void set_timeref_scale_factor(qint64 timeRefScaleFactor) {
        m_timeRefScaleFactor = timeRefScaleFactor;
    }
    qint64 get_timeref_scale_factor() const {
        return m_timeRefScaleFactor;
    }

private:
    qint64      m_timeRefScaleFactor{TTimeRef::UNIVERSAL_SAMPLE_RATE};
};

Q_DECLARE_OPERATORS_FOR_FLAGS(TViewPortInterface::ScrollModes)

#endif // TVIEWPORTINTERFACE_H
