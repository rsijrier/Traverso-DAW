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

#ifndef TCOMMAND_H
#define TCOMMAND_H

#include <QObject>
#include <QUndoCommand>

#ifndef Q_MOC_RUN
// Used to tell the Input Event Dispatchter this function is to allowed
// even if an hold action is active
#  define DISPATCH_RULE_IS_ALWAYS
#endif

class TContextItem;
class TContextPointer;
class QUndoStack;

class TCommand : public QObject, public QUndoCommand
{
    Q_OBJECT

public:
    TCommand(TContextItem* item, const QString& des = "No description set!");
    TCommand(const QString& des = "No description set!");
    virtual ~TCommand() override;

    enum ActionType {
        UNDO,
        DO
    };

    static bool matches_dispatch_rule_always(const QString& rule) {
        return rule == "DISPATCH_RULE_IS_ALWAYS";
    }

    virtual int begin_hold();
    virtual int finish_hold();
    virtual int prepare_actions();
    virtual int do_action();
    virtual int undo_action();
    virtual int jog();
    virtual void set_cursor_shape(int useX, int useY);
    virtual void cancel_action();
    virtual void process_collected_number(const QString& collected);
    virtual void set_jog_bypassed(bool bypassed) { Q_UNUSED(bypassed); }

    virtual bool is_hold_command() const { return true; }
    virtual bool supportsEnterFinishesHold() const { return true; }
    virtual bool wants_cursor_position_to_be_restored() const { return false; }

    void undo() override { undo_action(); }
    void redo() override { do_action(); }

    void set_valid(bool valid);
    void set_do_not_push_to_historystack();

    void set_context_pointer(TContextPointer* contextPointer) {
        m_contextPointer = contextPointer;
    }

    bool canvas_cursor_follows_mouse_cursor() const { return m_canvasCursorFollowsMouseCursor; }

    static void process_command(TCommand* cmd);

protected:
    QUndoStack*      m_historyStack{nullptr};
    TContextPointer* m_contextPointer{nullptr};
    bool             m_isValid{false};
    bool             m_canvasCursorFollowsMouseCursor{true};

private:
    friend class TInputEventDispatcher;
    int push_to_history_stack();
};

#endif // TCOMMAND_H
