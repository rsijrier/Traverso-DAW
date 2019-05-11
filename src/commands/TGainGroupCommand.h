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

#ifndef T_GAIN_GROUP_COMMAND_H
#define T_GAIN_GROUP_COMMAND_H

#include "TCommand.h"
#include <QPoint>

class Gain;

class TGainGroupCommand : public TCommand
{
    Q_OBJECT

public :
    TGainGroupCommand(ContextItem* context, const QVariantList& args);
    ~TGainGroupCommand();

    int begin_hold();
    int finish_hold();
    void cancel_action();
    void process_collected_number(const QString & collected);
    void set_cursor_shape(int useX, int useY);

    int jog();

    bool is_hold_command() const {return true;}
    int prepare_actions();
    int do_action();
    int undo_action();

    bool restoreCursorPosition() const {return true;}

    void add_command(Gain* cmd);

private:
    QList<Gain* >	m_gainCommands;
    Gain*           m_primaryGain;
    QPointF         m_origPos;
    ContextItem*    m_contextItem;
    bool            m_primaryGainOnly;


public slots:
    void increase_gain();
    void decrease_gain();
    void toggle_primary_gain_only();
};

#endif



