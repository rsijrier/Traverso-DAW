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

#ifndef MOVECURVENODE_H
#define MOVECURVENODE_H

#include "TMoveCommand.h"
#include "TTimeRef.h"
#include <QRectF>

class CurveView;
class CurveNode;
class Curve;
class QPoint;
class QRectF;

class MoveCurveNode : public TMoveCommand
{
    Q_OBJECT

public:
    MoveCurveNode(Curve* curve,
                  QList<CurveNode*> nodes,
                  float height,
                  qint64 scalefactor,
                  TTimeRef minWhenDiff,
                  TTimeRef maxWhenDiff,
                  double	minValueDiff,
                  double	maxValueDiff,
                  const QString& des);

    int prepare_actions();
    int do_action();
    int undo_action();
    int finish_hold();
    void cancel_action();
    int begin_hold();
    int jog();
    void set_cursor_shape(int useX, int useY);

    void set_height(int height) {
        mcnd->height = height;
    }

    int get_height() {
        return mcnd->height;
    }

private :
    struct	MoveCurveNodeData {
        qint64		scalefactor;
        QPoint		mousepos;
        bool		verticalOnly;
        float		height;
        double		maxValueDiff;
        double		minValueDiff;
        TTimeRef		maxWhenDiff;
        TTimeRef		minWhenDiff;
    };

    MoveCurveNode::MoveCurveNodeData* mcnd;

    struct CurveNodeData {
        CurveNode* node;
        double	origWhen;
        double	origValue;
    };

    double	m_valueDiff;
    TTimeRef	m_whenDiff;

    QList<CurveNodeData> m_nodeDatas;

    int check_and_apply_when_and_value_diffs();


public slots:
    void move_up();
    void move_down();
    void move_left();
    void move_right();
    void toggle_vertical_only();
};

#endif // MOVECURVENODE_H
