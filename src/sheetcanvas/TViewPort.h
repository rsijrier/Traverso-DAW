/*
    Copyright (C) 2005-2006 Remon Sijrier

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

#pragma once

#include <QGraphicsView>
#include <QTimer>
#include "TViewPortInterface.h"
#include "TInteractionCursors.h"

class TSheetView;

class TViewPort : public QGraphicsView, public TViewPortInterface
{
    Q_OBJECT

public:
    TViewPort(QGraphicsScene* scene, QWidget* parent);
    virtual ~TViewPort();

    void set_canvas_cursor_text(const QString& text, int mseconds = -1) final;
    void set_canvas_cursor_pos(QPointF pos, CursorMoveReason reason) final;
    void set_canvas_cursor_type(TContextPointer::CursorType) final;
    void set_canvas_cursor_controller_type(TraversoDAW::CursorType cursorType) final;
    void set_canvas_cursor_data(const QVariant& data) final;
    void set_canvas_cursor_location(const TTimeRef &location, qreal sceneY) final;
    virtual void set_sheetview(TSheetView* view) { m_sv = view; }

    QPointF map_to_scene(const QPoint& pos) const final { return mapToScene(pos); }
    QPoint map_to_global(const QPoint& point) const final { return mapToGlobal(point); }

    void grab_mouse() final;
    void release_mouse() final;
    void detect_items_below_cursor() final;

    void scroll_horizontal(int pixels) final;
    void scroll_vertical(int pixels) final;
    int viewport_width() const final;
    int viewport_height() const final;
    void refresh() final;

protected:
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;

    bool event(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;

    TSheetView* m_sv;

private:
    QPoint      m_previousMousePos;
    QTimer      m_grabMouseGuardTimer;
    int         m_mouseGrabCheckTime;
};
