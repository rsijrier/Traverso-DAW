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

    $Id: Command.h,v 1.13 2008/02/12 20:39:08 r_sijrier Exp $
*/

#ifndef COMMAND_H
#define COMMAND_H

#include <QObject>
#include <QUndoCommand>
#include <QUndoStack>

class ContextItem;
class QUndoStack;

class TCommand : public QObject, public QUndoCommand
{
    Q_OBJECT

public :
    TCommand(ContextItem* item, const QString& des = "No description set!");
    TCommand(const QString& des = "No description set!");
    virtual ~TCommand();

    enum ActionType {
        UNDO,
        DO
    };

    virtual int begin_hold();
    virtual int finish_hold();
    virtual int prepare_actions();
    virtual int do_action();
    virtual int undo_action();
    virtual int jog();
    virtual void set_cursor_shape(int useX, int useY);
    virtual void cancel_action();
    virtual void process_collected_number(const QString& collected);
    virtual void set_jog_bypassed(bool /*bypassed*/) {}
    virtual bool is_hold_command() const {return true;}
    virtual bool supportsEnterFinishesHold() const {return true;}
    virtual bool restoreCursorPosition() const {return false;}

    void undo() {undo_action();}
    void redo() {do_action();}

    void set_valid(bool valid);
    void set_do_not_push_to_historystack();
    bool canvas_cursor_follows_mouse_cursor() const {return m_canvasCursorFollowsMouseCursor;}

    static void process_command(TCommand* cmd);


protected:
    bool 		m_isValid;
    bool        m_canvasCursorFollowsMouseCursor;

private:
    QUndoStack* m_historyStack;

    friend class TInputEventDispatcher;
    int push_to_history_stack();
};


#endif


