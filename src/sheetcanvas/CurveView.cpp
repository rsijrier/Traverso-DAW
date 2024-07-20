/*
Copyright (C) 2006-2007 Remon Sijrier

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

#include "CurveView.h"
#include "SheetView.h"
#include "CurveNodeView.h"
#include <Themer.h>

#include <Curve.h>
#include <CurveNode.h>
#include <ContextPointer.h>
#include <Sheet.h>
#include "TInputEventDispatcher.h"

#include <AddRemove.h>
#include "CommandGroup.h"
#include "MoveCurveNode.h"

#include <Debugger.h>


#define NODE_SOFT_SELECTION_DISTANCE 40


#include <cfloat>

CurveView::CurveView(SheetView* sv, ViewItem* parentViewItem, Curve* curve)
    : ViewItem(parentViewItem, curve)
    , m_curve(curve)
{
    setZValue(parentViewItem->zValue() + 1);
    setFlags(QGraphicsItem::ItemUsesExtendedStyleOption);

    m_sv = sv;
    CurveView::load_theme_data();

    m_blinkColorDirection = 1;
    m_blinkingNode = nullptr;
    m_startoffset = TTimeRef();
    m_guicurve = new Curve(nullptr);
    m_guicurve->set_sheet(sv->get_sheet());

    for(CurveNode* node = m_curve->get_nodes().first(); node != nullptr; node = node->next) {
        add_curvenode_view(node);
    }

    connect(&m_blinkTimer, SIGNAL(timeout()), this, SLOT(update_blink_color()));
    connect(m_curve, SIGNAL(nodeAdded(CurveNode*)), this, SLOT(add_curvenode_view(CurveNode*)));
    connect(m_curve, SIGNAL(nodeRemoved(CurveNode*)), this, SLOT(remove_curvenode_view(CurveNode*)));
    connect(m_curve, SIGNAL(nodePositionChanged()), this, SLOT(node_moved()));
    connect(m_curve, SIGNAL(activeContextChanged()), this, SLOT(active_context_changed()));

    m_hasMouseTracking = true;
}

CurveView::~ CurveView( )
{
    m_guicurve->clear_curve();
    delete m_guicurve;
}

void CurveView::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
    Q_UNUSED(widget);
    PENTER2;
    if (m_nodeViews.isEmpty()) {
        return;
    }

    if (item_ignores_context() && m_nodeViews.size() == 1) {
        return;
    }

    painter->save();

    int xstart = int(option->exposedRect.x());
    int pixelcount = int(option->exposedRect.width()+1);
    int height = int(m_boundingRect.height());
    int offset = int(m_startoffset / m_sv->timeref_scalefactor);


    QPen pen;

    QColor penColor;
    // NOTE: setting penwidth to 2 increases cpu load by at least 5 times for this function
    pen.setWidth(1);
    if (item_ignores_context()) {
        penColor = themer()->get_color("Curve:inactive");
    } else {
         penColor = themer()->get_color("Curve:active");
         QColor backgroundColor(0, 0, 0, 30);
         painter->fillRect(option->exposedRect, QBrush(backgroundColor));
    }
    pen.setColor(penColor);
    painter->setPen(pen);
    //	painter->setClipRect(m_boundingRect);


    if (m_nodeViews.size() == 1) {
        int y = int(height - (m_nodeViews.first()->get_value() * height));
        painter->drawLine(xstart, y, xstart + pixelcount, y);
        painter->restore();
        return;
    }

    Q_ASSERT(m_nodeViews.size() > 0);

    CurveNodeView* firstNodeView = m_nodeViews.constFirst();
    if (firstNodeView->get_when() > xstart) {
        int y = int(height - (firstNodeView->get_value() * height));
        int length = int(firstNodeView->get_when()) - xstart - offset;
        if (length > 0) {
            painter->drawLine(xstart, y, xstart + length, y);
            xstart += length;
            pixelcount -= length;
        }
        if (pixelcount <= 0) {
            painter->restore();
            return;
        }
    }

    CurveNodeView* lastNodeView = m_nodeViews.constLast();
    if (lastNodeView->get_when() < (xstart + pixelcount + offset)) {
        int y = int(height - (lastNodeView->get_value() * height));
        int x = int(lastNodeView->get_when()) - offset;
        int length = (xstart + pixelcount) - int(lastNodeView->get_when()) + offset;
        if (length > 0) {
            painter->drawLine(x, y, x + length - 1, y);
            pixelcount -= length;
        }
        if (pixelcount <= 0) {
            painter->restore();
            return;
        }
    }


    // Path's need an additional pixel righ/left to be painted correctly.
    // FadeCurveView get_curve adjusts for this, if changing these
    // values, also change the adjustment in FadeCurveView::get_curve() !!!
    pixelcount += 2;
    xstart -= 1;
    if (xstart < 0) {
        xstart = 0;
    }

    painter->setRenderHint(QPainter::Antialiasing);

    QPolygonF polygon;
    auto buffer = QVarLengthArray<float, 40>(pixelcount);

    // 	printf("range: %d\n", (int)m_nodeViews.last()->pos().x());
    m_guicurve->get_vector(xstart + offset,
                           xstart + pixelcount + offset,
                           buffer.data(),
                           nframes_t(pixelcount));

    for (int i=0; i<pixelcount; i+=3) {
        polygon <<  QPointF(xstart + i, height - qreal(buffer.at(i) * height) );
    }

    // Depending on the zoom level, curve nodes can end up to be aligned
    // vertically at the exact same x position. The curve line won't be painted
    // by the routine above (it doesn't catch the second node position obviously)
    // so we add curvenodes _always_ to solve this problem easily :-)
    for(CurveNodeView* view : m_nodeViews) {
        qreal x = view->x();
        if ( (x > xstart) && x < (xstart + pixelcount)) {
            polygon <<  QPointF( x + view->boundingRect().width() / 2,
                                 (height - (view->get_curve_node()->get_value() * height)) );
        }
    }

    // Which means we have to sort the polygon
    std::sort(polygon.begin(), polygon.end(), [&](const QPointF &left, const QPointF &right){
        return left.x() < right.x();
    });

    painter->drawPolyline(polygon);
    painter->restore();
}

int CurveView::get_vector(qreal xstart, qreal pixelcount, float* arg)
{
    if (m_guicurve->get_nodes().size() == 1 && m_guicurve->get_nodes().first()->get_value() == 1.0) {
        return 0;
    }

    m_guicurve->get_vector(xstart, xstart + pixelcount, arg, nframes_t(pixelcount));

    return 1;
}

void CurveView::add_curvenode_view(CurveNode* node)
{
    CurveNodeView* nodeview = new CurveNodeView(m_sv, this, node, m_guicurve);
    m_nodeViews.append(nodeview);

    AddRemove* cmd = qobject_cast<AddRemove*>(m_guicurve->add_node(nodeview, false));
    if (cmd) {
        cmd->set_instantanious(true);
        TCommand::process_command(cmd);

        std::sort(m_nodeViews.begin(), m_nodeViews.end(), [&](CurveNodeView* left, CurveNodeView* right){
            return left->get_when() < right->get_when();
        });

        update();
    }
}

void CurveView::remove_curvenode_view(CurveNode* node)
{
    for(CurveNodeView* nodeview : m_nodeViews) {
        if (nodeview->get_curve_node() == node) {
            m_nodeViews.removeAll(nodeview);
            if (nodeview == m_blinkingNode) {
                m_blinkingNode = nullptr;
                update_softselected_node(cpointer().scene_pos());
            }
            AddRemove* cmd = qobject_cast<AddRemove*>(m_guicurve->remove_node(nodeview, false));
            if (cmd) {
                cmd->set_instantanious(true);
                TCommand::process_command(cmd);

                scene()->removeItem(nodeview);
                delete nodeview;
                update();
                return;
            }
        }
    }
}

void CurveView::calculate_bounding_rect()
{
    // Add a bit of top/bottom margin so the curve line doesn't go all the
    // way to the top/bottom of the view, and as a side effect, the nodes
    // can be painted in the margin area, so they won't chop off.
    m_boundingRect = QRectF(0, 0, m_parentViewItem->boundingRect().width(), m_parentViewItem->get_height() - BORDER_MARGIN);
    setPos(0, BORDER_MARGIN / 2);
    ViewItem::calculate_bounding_rect();
}

void CurveView::active_context_changed()
{
    if (has_active_context()) {
        m_blinkTimer.start(40);
    } else {
        if (ied().is_holding()) {
            printf("returning in active context changed\n");
            return;
        }

        m_blinkTimer.stop();
        if (m_blinkingNode) {
            m_blinkingNode->set_color(themer()->get_color("CurveNode:default"));
            m_blinkingNode->set_soft_selected(false);
            m_blinkingNode = nullptr;
        }

    }

    update(m_boundingRect);
}


void CurveView::mouse_hover_move_event()
{
    update_softselected_node(cpointer().scene_pos());

    if (m_blinkingNode) {
        QString shape = m_sv->cursor_dict()->value("CurveNodeView", "");
        cpointer().set_canvas_cursor_shape(shape, Qt::AlignTop | Qt::AlignHCenter);
    } else {
        QString shape = m_sv->cursor_dict()->value("CurveView", "");
        cpointer().set_canvas_cursor_shape(shape, Qt::AlignTop | Qt::AlignHCenter);
    }
}


void CurveView::update_softselected_node(QPointF point)
{
    if (m_nodeViews.isEmpty()) {
        return;
    }

    QPointF pos = mapToItem(this, point);

    CurveNodeView* prevNode = m_blinkingNode;
    m_blinkingNode = m_nodeViews.first();

    if (! m_blinkingNode)
        return;

    foreach(CurveNodeView* nodeView, m_nodeViews) {

        QPointF nodePos(nodeView->scenePos().x(), nodeView->scenePos().y());

        qreal nodeDist = (pos - nodePos).manhattanLength();
        qreal blinkNodeDist = (pos - QPointF(m_blinkingNode->scenePos().x(), m_blinkingNode->scenePos().y())).manhattanLength();

        if (nodeDist < blinkNodeDist) {
            m_blinkingNode = nodeView;
        }
    }

    if ((pos - QPointF(4, 4) - QPointF(m_blinkingNode->scenePos().x(), m_blinkingNode->scenePos().y())).manhattanLength() > NODE_SOFT_SELECTION_DISTANCE) {
        m_blinkingNode = nullptr;
    }

    if (prevNode && (prevNode != m_blinkingNode) ) {
        prevNode->set_color(themer()->get_color("CurveNode:default"));
        prevNode->update();
        prevNode->set_soft_selected(false);
        if (m_blinkingNode) {
            m_blinkingNode->set_soft_selected(true);
        }
    }
    if (!prevNode && m_blinkingNode) {
        m_blinkingNode->set_soft_selected(true);
        m_blinkDarkness = 100;
    }
}


void CurveView::update_blink_color()
{
    if (!m_blinkingNode) {
        return;
    }

    m_blinkDarkness += (6 * m_blinkColorDirection);

    if (m_blinkDarkness >= 100) {
        m_blinkColorDirection *= -1;
        m_blinkDarkness = 100;
    } else if (m_blinkDarkness <= 40) {
        m_blinkColorDirection *= -1;
        m_blinkDarkness = 40;
    }

    QColor blinkColor = themer()->get_color("CurveNode:blink");

    m_blinkingNode->set_color(blinkColor.lighter(m_blinkDarkness));

    m_blinkingNode->update();
}


TCommand* CurveView::add_node()
{
    PENTER;
    QPointF point = mapFromScene(cpointer().scene_pos());

    emit curveModified();

    double when = point.x() * double(m_sv->timeref_scalefactor) + m_startoffset.universal_frame();
    double value = (m_boundingRect.height() - point.y()) / m_boundingRect.height();

    CurveNode* node = new CurveNode(m_curve, when, value);

    return m_curve->add_node(node);
}


TCommand* CurveView::remove_node()
{
    PENTER;

    update_softselected_node(cpointer().on_first_input_event_scene_pos());

    QList<CurveNodeView*> nodesToBeRemoved = get_selected_nodes();


    if (!nodesToBeRemoved.size()) {
        return ied().failure();
    }

    emit curveModified();

    QString description = tr("Removed %n Node(s)", "", nodesToBeRemoved.size());
    CommandGroup* group = new CommandGroup(m_curve, description);

    foreach(CurveNodeView* nodeView, nodesToBeRemoved) {
        nodeView->set_hard_selected(false);
        group->add_command(m_curve->remove_node(nodeView->get_curve_node()));
    }

    return group;
}

TCommand* CurveView::drag_node()
{
    PENTER;

    update_softselected_node(cpointer().on_first_input_event_scene_pos());

    QList<CurveNodeView*> selectedNodeViews = get_selected_nodes();
    QList<CurveNode*> selectedNodes;
    foreach(CurveNodeView* nodeView, selectedNodeViews) {
        selectedNodes.append(nodeView->get_curve_node());
    }

    if (!selectedNodes.size()) {
        return ied().failure();
    }

    TTimeRef min(qint64(0));
    TTimeRef max(qint64(DBL_MAX));
    TRealTimeLinkedList<CurveNode*> nodeList = m_curve->get_nodes();

    int indexFirstNode = nodeList.indexOf(selectedNodes.first());
    int indexLastNode = nodeList.indexOf(selectedNodes.last());

    if (indexFirstNode > 0) {
        min = TTimeRef(((CurveNode*)nodeList.at(indexFirstNode-1))->get_when() + 1);
    }

    if (nodeList.size() > (indexLastNode + 1)) {
        max = TTimeRef(((CurveNode*)nodeList.at(indexLastNode+1))->get_when() - 1);
    }

    if (boundingRect().width() * m_sv->timeref_scalefactor < max) {
        max = boundingRect().width() * m_sv->timeref_scalefactor;
    }

    if ((min - get_start_offset()) < TTimeRef()) {
        min = get_start_offset();
    }

    TTimeRef startLocation = TTimeRef(selectedNodes.first()->get_when());
    TTimeRef endLocation  = TTimeRef(selectedNodes.last()->get_when());
    TTimeRef minWhenDiff = min - startLocation;
    TTimeRef maxWhenDiff = max - endLocation + m_startoffset;


    double maxValue = DBL_MIN;
    double minValue = DBL_MAX;
    foreach(CurveNode* node, selectedNodes) {
        double value = node->get_value();
        if (value > maxValue) {
            maxValue = value;
        }
        if (value < minValue) {
            minValue = value;
        }
    }

    double minValueDiff = 0 - minValue;
    double maxValuediff = 1 - maxValue;

    QString text = tr("Move Curve Node(s)", "", selectedNodes.size());

    emit curveModified();

    return new MoveCurveNode(m_curve, selectedNodes, boundingRect().height(), m_sv->timeref_scalefactor,
                             minWhenDiff, maxWhenDiff, minValueDiff, maxValuediff, text);
}

void CurveView::node_moved( )
{
    CurveNodeView* prev = nullptr;
    CurveNodeView* next = nullptr;

    QList<CurveNodeView*> selectedNodes = get_selected_nodes();

    if (!selectedNodes.size()) {
        // even though there are no selected nodes, a curve node did move
        // e.g. by an undo action, so at least update the view
        update();
        return;
    }

    CurveNodeView* firstSelectedNodeView = selectedNodes.first();
    CurveNodeView* lastSelectedNodeView = selectedNodes.last();

    int xleft = (int) firstSelectedNodeView->x();
    int xright = (int) lastSelectedNodeView->x();
    int leftindex = m_nodeViews.indexOf(firstSelectedNodeView);
    int rightindex = m_nodeViews.indexOf(lastSelectedNodeView);
    int count = 0;


    if (firstSelectedNodeView == m_nodeViews.first()) {
        xleft = 0;
    } else {
        while ( leftindex > 0 && count < 2) {
            leftindex--;
            count++;
        }
        prev = m_nodeViews.at(leftindex);
    }


    count = 0;

    if (lastSelectedNodeView == m_nodeViews.last()) {
        xright = (int) m_boundingRect.width();
    } else {
        while (rightindex < (m_nodeViews.size() - 1) && count < 2) {
            rightindex++;
            count++;
        }
        next = m_nodeViews.at(rightindex);
    }


    if (prev) xleft = (int) prev->x();
    if (next) xright = (int) next->x();


    update(xleft, 0, xright - xleft + 3, m_boundingRect.height());
}

void CurveView::load_theme_data()
{
    CurveView::calculate_bounding_rect();
}

void CurveView::set_start_offset(TTimeRef offset)
{
    m_startoffset = offset;
}

bool CurveView::has_nodes() const
{
    return m_guicurve->get_nodes().size() > 1 ? true : false;
}

float CurveView::get_default_value()
{
    if (m_guicurve->get_nodes().isEmpty()) {
        return 1.0f;
    }

    return float(m_guicurve->get_nodes().first()->get_value());
}

TCommand * CurveView::remove_all_nodes()
{
    CommandGroup* group = new CommandGroup(m_curve, tr("Clear Nodes"));

    for(CurveNode* node = m_curve->get_nodes().first(); node != nullptr; node = node->next) {
        group->add_command(m_curve->remove_node(node));
    }

    return group;
}

TCommand* CurveView::select_lazy_selected_node()
{
    if (!m_blinkingNode)
    {
        return ied().failure();
    }

    m_blinkingNode->set_hard_selected(!m_blinkingNode->is_hard_selected());

    return ied().succes();
}

TCommand* CurveView::toggle_select_all_nodes()
{
    bool selectedNodes = false;
    foreach(CurveNodeView* nodeView, m_nodeViews) {
        if (nodeView->is_hard_selected()) {
            selectedNodes = true;
            break;
        }
    }

    if (selectedNodes) {
        foreach(CurveNodeView* nodeView, m_nodeViews) {
            nodeView->set_hard_selected(false);
        }
    } else {
        foreach(CurveNodeView* nodeView, m_nodeViews) {
            nodeView->set_hard_selected(true);
        }
    }

    update();

    return ied().succes();
}

CurveNodeView* CurveView::get_node_view_before(TTimeRef location) const
{
    TTimeRef curveStartOffset = m_curve->get_start_offset();

    for (int i = m_nodeViews.size() - 1; i>=0; --i) {
        CurveNodeView* nodeview = m_nodeViews.at(i);
        TTimeRef absoluteLocation = TTimeRef(nodeview->get_curve_node()->get_when()) + curveStartOffset;
        if (absoluteLocation < location) {
            return nodeview;
        }
    }

    return nullptr;
}

CurveNodeView* CurveView::get_node_view_after(TTimeRef location) const
{
    TTimeRef curveStartOffset = m_curve->get_start_offset();

    foreach(CurveNodeView* nodeview, m_nodeViews) {
        TTimeRef absoluteLocation = TTimeRef(nodeview->get_curve_node()->get_when()) + curveStartOffset;
        if (absoluteLocation > location) {
            return nodeview;
        }
    }

    return nullptr;
}

QString CurveView::get_name() const
{
    return "Gain Envelope";
}

QList<CurveNodeView*> CurveView::get_selected_nodes()
{
    QList<CurveNodeView*> list;

    foreach(CurveNodeView* curveNodeView, m_nodeViews) {
        if (curveNodeView->is_hard_selected()) {
            list.append(curveNodeView);
        }
    }

    if (list.empty() && m_blinkingNode) {
        list.append(m_blinkingNode);

    }

    return list;
}
