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

    virtual QPointF map_to_scene(const QPoint& pos) const = 0;
    virtual int get_current_mode() const = 0;
    virtual void setCanvasCursorShape(const QString& cursor, int alignment=Qt::AlignCenter) = 0;
    virtual void setCursorText(const QString& text, int mseconds) = 0;
    virtual void set_holdcursor_pos(QPointF pos) = 0;

    virtual void prepare_for_shortcut_dispatch() = 0;
    virtual void grab_mouse() = 0;
    virtual void release_mouse() = 0;
};

#endif // ABSTRACTVIEWPORT_H
