/*
    Copyright (C) 2009 Remon Sijrier

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

#ifndef CROP_H
#define CROP_H

#include "TCommand.h"

#include <memory>

class TAudioClipView;
class TAudioTrack;
class TAudioClip;
class QGraphicsRectItem;
class QUndoCommand;

class CropClip : public TCommand
{
        Q_OBJECT

public :
        CropClip(TAudioClipView* cv);
        ~CropClip();

        int begin_hold();
        int finish_hold();
        int prepare_actions();
        int do_action();
        int undo_action();
        void cancel_action();

        int jog();

private:
        TAudioClipView* m_cv;
        TAudioTrack* m_track;
        TAudioClip* m_clip;
        TAudioClip* leftClip{};
        TAudioClip* rightClip{};
        std::unique_ptr<QUndoCommand> m_mutation;
        QGraphicsRectItem* m_selection;
        qint64 x1;
        qint64 x2;

public slots:
        void adjust_left();
        void adjust_right();
};

#endif


