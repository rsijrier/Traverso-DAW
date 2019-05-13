#ifndef ABSTRACTVIEWPORT_H
#define ABSTRACTVIEWPORT_H

#include <QPointF>
#include <QList>

class ContextItem;

class AbstractViewPort
{
public:
    AbstractViewPort();
    virtual ~AbstractViewPort();

    enum CursorMoveReason {
        UNDEFINED,
        MOUSE_MOVE_EVENT,
        KEYBOARD_NAVIGATION
    };

    virtual QPointF map_to_scene(const QPoint& pos) const = 0;
    virtual void setCanvasCursorShape(const QString& cursor, int alignment=Qt::AlignCenter) = 0;
    virtual void setCursorText(const QString& text, int mseconds) = 0;
    virtual void set_holdcursor_pos(QPointF pos, CursorMoveReason reason = UNDEFINED) = 0;
    virtual void detect_items_below_cursor() = 0;

    virtual void grab_mouse() = 0;
    virtual void release_mouse() = 0;
};

#endif // ABSTRACTVIEWPORT_H
