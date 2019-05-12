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

#include "MovePlugin.h"

#include "PluginView.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

MovePlugin::MovePlugin(PluginView* view)
    : TCommand(view->get_context(), "")
    , m_pluginView(view)
{
}

MovePlugin::~MovePlugin()
{
}

int MovePlugin::begin_hold()
{
    m_sceneXStartPos = cpointer().on_first_input_event_scene_x();
    m_pluginViewOrigXPos = m_pluginView->x();
    m_pluginView->set_moving(true);

    return 1;
}


int MovePlugin::finish_hold()
{
    m_pluginView->set_moving(false);

    return 1;
}


int MovePlugin::prepare_actions()
{
    return -1;
}


int MovePlugin::do_action()
{
    PENTER;

    return 1;
}


int MovePlugin::undo_action()
{
    PENTER;

    return 1;
}

void MovePlugin::cancel_action()
{
    finish_hold();
    undo_action();
}

int MovePlugin::jog()
{
    qreal diff = m_sceneXStartPos - cpointer().scene_x();
    qreal newXPos = m_pluginViewOrigXPos - diff;
    if (newXPos < 0) {
        newXPos = 0;
    }
    m_pluginView->setX(newXPos);

    return 1;
}


void MovePlugin::move_left()
{
}

void MovePlugin::move_right()
{
}

