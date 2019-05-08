/*
    Copyright (C) 2019 Remon Sijrier

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

#ifndef MOVEPLUGIN_H
#define MOVEPLUGIN_H


#include "TCommand.h"

class PluginView;

class MovePlugin : public TCommand
{
    Q_OBJECT

public :
    MovePlugin(PluginView* view);
    ~MovePlugin();

    int begin_hold();
    int finish_hold();
    int prepare_actions();
    int do_action();
    int undo_action();
    void cancel_action();
    int jog();

private:
    PluginView*      m_pluginView;

public slots:
    void move_left();
    void move_right();
};


#endif // MOVEPLUGIN_H
