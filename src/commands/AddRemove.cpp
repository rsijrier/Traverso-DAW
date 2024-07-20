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

$Id: AddRemove.cpp,v 1.6 2008/11/24 10:12:19 r_sijrier Exp $
*/

#include "AddRemove.h"
#include "ContextItem.h"
#include <Sheet.h>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

/** 	\class AddRemove 
 *	\brief Historably add/remove objects into the audio processing path without using locks

    AddRemove is a flexible class that let's you insert/remove objects into the audio <br />
    processing execution path without using locks.<br />
    It can also be used with objects that aren't in the audio processing chain, and don't <br />
    need the thread safety. Use set_instantanious() to let the created AddRemove object<br />
    know that it can bypass the thread save logic, and call the add/remove functions directly.

    The example below is typical how this Command class should be used.

    One for example creates the function (slot) Command* add_track() in SheetView. and add <br />
    an entry in the keymap file to define which keyfact will be used to call this function.<br />
    One also can create a TCommandPlugin, and point the keyfact in the keymap file to the <br />
    plugin name, this way you don't need to add a function (slot) to the SheetView class.

    When using the first approach, you create a new Track object in the GUI object SheetView<br />
    and return the Command object returned by m_sheet->add_track(track);, see example code below.

    Using a TCommandPlugin for this kind of action doesn't make much sense, you most likely <br />
    want to use plugins to manipulate existing objects.

    \code
    class Sheet : public ContextItem
    {
        Q_OBJECT
        Sheet() {};
        ~Sheet() {};

    private:
        list<Track*> m_tracks;

    public slots:
        Command* add_track(Track* track);
        Command* remove_track(Track* track);

    private slots:
        void private_add_track(Track*);
        void private_remove_track(Track*);
    }


    Command* Sheet::add_track(Track* track)
    {
        // first this: 	The object to which the track has to be added/removed too
        // track: 	the argument that will be used by the signal/slots as specified in the second and third line.
        // true: 	this command should be considered historable
        // this: 	A pointer to Sheet, which in this case is this, which will be used in the AddRemove
        // 		Command logic to detect if the private_add/remove slots can be called directly or
        //		thread save via Tsar's thread save logic.
        // tr("Add Track")	The (tranlated) description of this action as it will show up in the HistoryView
        return new AddRemove(this, track, true, this,
            "private_add_track(Track*)", "trackAdded(Track*)",
            "private_remove_track(Track*)", "trackRemoved(Track*)",
            tr("Add Track"));
    }

    Command* Sheet::remove_track(Track* track)
    {
        // Same applies as in add_track(), however, the second and third line are switched :-)
        return new AddRemove(this, track, true, this,
            "private_remove_track(Track*)", "trackRemoved(Track*)",
            "private_add_track(Track*)", "trackAdded(Track*)",
            tr("Remove Track"));
    }

    void Sheet::private_add_track(Track* track)
    {
        m_tracks.append(track);
    }

    void Sheet::private_remove_track(Track* track)
    {
        m_tracks.removeAll(track);
    }


    // Example usage in a GUI object.
    Command* SheetView::add_track()
    {
        Track* track = new Track(m_sheet);
        return m_sheet->add_track(track);
    }

    \endcode


 */


AddRemove::AddRemove(ContextItem* parent, ContextItem* item, const QString& des)
    : TCommand(parent, des),
      m_sheet(nullptr),
      m_doActionSlot(""),
      m_undoActionSlot(""),
      m_doSignal(""),
      m_undoSignal(""),
      m_instantanious(false)
{
    m_parentItem = parent;
    m_arg = item;

    if (item && item->has_active_context()) {
        cpointer().remove_from_active_context_list(item);
    }
}


/**
 * 			Constructor with all the paramaters needed for proper operation.
 *
 * @param parent 	The object (which need to inherit ContextItem) where the arg
            will be added or removed.
 * @param arg	 	The object that will be added/removed
 * @param historable 	Makes the command historable if set to true, else it will be deleted
            after the do_action call. Only works when the command was requested
            from the InputEngine. If not, you are responsible yourself to call the
            do_action, and pushing it to the correct history stack.
 * @param sheet 		If a related Sheet object is available you can use it, else supply a 0
 * @param doActionSlot 	(private) slot signature which will be called on do_action()
 * @param doSignal 	If supplied, the signal with this signature will be emited in the GUI thread
            AFTER the actuall  adding/removing action in do_action() has happened!
 * @param undoActionSlot (private) slot signature which will be called in undo_action();
 * @param undoSignal 	If supplied, the signal with this signature will be emited in the GUI thread
            AFTER the actuall adding/removing in undo_action has happened!
 * @param des 		Short description that will show up in the history view.
 */
AddRemove::AddRemove(
        ContextItem* parent,
        void* arg,
        bool historable,
        TSession* sheet,
        const char * doActionSlot,
        const char * doSignal,
        const char * undoActionSlot,
        const char * undoSignal,
        const QString& des)
    : TCommand(parent, des),
      m_parentItem(parent),
      m_arg(arg),
      m_sheet(sheet),
      m_doActionSlot(doActionSlot),
      m_undoActionSlot(undoActionSlot),
      m_doSignal(doSignal),
      m_undoSignal(undoSignal),
      m_instantanious(false)
{
    if (!historable) {
        set_do_not_push_to_historystack();
    }
}

AddRemove::AddRemove(
        ContextItem* parent,
        ContextItem* item,
        bool historable,
        TSession* sheet,
        const char * doActionSlot,
        const char * doSignal,
        const char * undoActionSlot,
        const char * undoSignal,
        const QString& des)
    : TCommand(parent, des),
      m_parentItem(parent),
      m_arg(item),
      m_sheet(sheet),
      m_doActionSlot(doActionSlot),
      m_undoActionSlot(undoActionSlot),
      m_doSignal(doSignal),
      m_undoSignal(undoSignal),
      m_instantanious(false)
{
    if (!historable) {
        set_do_not_push_to_historystack();
    }

    if (item && item->has_active_context()) {
        cpointer().remove_from_active_context_list(item);
    }
}


AddRemove::~AddRemove()
{}

int AddRemove::prepare_actions()
{
    Q_ASSERT(m_parentItem);
    Q_ASSERT(m_arg);
    Q_ASSERT(m_doActionSlot != QString(""));
    Q_ASSERT(m_undoActionSlot != QString(""));

    tsar().prepare_event(m_doActionEvent, m_parentItem, m_arg, m_doActionSlot, m_doSignal);
    tsar().prepare_event(m_undoActionEvent, m_parentItem, m_arg, m_undoActionSlot, m_undoSignal);

    return 1;
}

int AddRemove::do_action()
{
    PENTER3;
    return un_redo_action(TCommand::ActionType::DO);
}

int AddRemove::undo_action()
{
    PENTER3;

    return un_redo_action(TCommand::ActionType::UNDO);
}

int AddRemove::un_redo_action(ActionType actionType)
{
    TsarEvent event;
    switch (actionType) {

    case TCommand::UNDO:
        event = m_undoActionEvent;
        break;
    case TCommand::DO:
        event = m_doActionEvent;
        break;
    }

    if (m_instantanious) {
        tsar().process_event(event);
        return 1;
    }

    if (m_sheet) {
        if (m_sheet->is_transport_rolling()) {
            PMESG("AddRemove::un_redo_action: Using Thread Save add/remove");
            tsar().post_gui_event(event);
        } else {
            tsar().process_event(event);
        }
    } else {
        PMESG("Using direct add/remove/signaling");
        tsar().post_gui_event(event);
    }

    // update the cursor to the context item that we are adding here
    // in case the mouse cursor did not move and the position of this object
    // is below the mouse cursor position
    // cpointer().request_viewport_to_detect_items_below_cursor();

    return 1;
}


    /**
 * 	Set's the command as instantanious
 
    The do/undo actions will call the slot and emit the signal (if they exist)
    directly, and thus bypassing the RT thread save nature of Tsar.
 */
void AddRemove::set_instantanious(bool instant)
{
    m_instantanious = instant;
}

#include "AddRemove.moc"

// eof
