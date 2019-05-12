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

#ifndef MOVE_COMMAND_H
#define MOVE_COMMAND_H

#include "TCommand.h"

#include <QEasingCurve>
#include <QTimer>

class SheetView;

class TMoveCommand : public TCommand
{
    Q_OBJECT

public :
    TMoveCommand (SheetView* sv, ContextItem* item, const QString& description);
    virtual ~TMoveCommand (){}

    int begin_hold();
    int finish_hold();
    void cancel_action();
    int jog();
    void process_collected_number(const QString & collected);

protected :
    void start_shuttle(bool drag=false);
    void stop_shuttle();
    void update_shuttle_factor();
    void set_shuttle_factor_values(int x, int y);

    struct Data {
        SheetView*      sv;
        QTimer			shuttleTimer;
        QEasingCurve    shuttleCurve;
        QEasingCurve    dragShuttleCurve;
        bool			dragShuttle{};
        int             shuttleXfactor{};
        int             shuttleYfactor{};
        int             speed;
        bool            doSnap;
    };

    Data* d;

private:
    void cleanup_and_free_data();

public slots:
    void move_faster();
    void move_slower();
    void toggle_snap_on_off();

private slots:
    void update_shuttle();


};

#endif // MOVECOMMAND_H
