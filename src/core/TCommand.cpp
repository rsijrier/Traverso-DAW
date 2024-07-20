/*
Copyright (C) 2005-2007 Remon Sijrier 

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

$Id: Command.cpp,v 1.21 2008/02/12 20:39:08 r_sijrier Exp $
*/

#include "TCommand.h"
#include "ContextPointer.h"
#include <AbstractViewPort.h>
#include <Utils.h>
#include <Themer.h>
#include "ContextItem.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

/**
	\class Command
	\brief An Interface class for creating ('analog', or holding command type) un/redoable actions

	Traverso uses the Command pattern for 2 purposes: Creating historable actions
 	and the so called 'hold actions', providing an easy way to create 'analog' user
	interface interactions.

	There are 2 types of Commands possible, the 'Single fact' type, which 
 	only needs to reimplement the do_action(), undo_action() and prepare_actions() 
	functions, and the "Hold fact' type, which also reimplements the jog() function, 
	together with the begin_hold() and  finish_hold() functions.
	
	<br/>
	<b>Single Fact type function call order:</b>

	- prepare_actions(), if it returns -1, the InputEngine will recognise it as a
	failed command, and delete it.
	<br/>
	- do_action(), the actuall action to be performed will happen here

	<br/>
	<b>Hold Fact type function call order:</b>
	
	- begin_hold(), if it returns -1, the InputEngine will recognise it as a
	failed command, and delete it.
	<br/>
	- set_cursor_shape(), the default mouse gesture cursor is used, reimplement
	to set a custom one, or to use the advanced HoldCursor of the ViewPort
	<br/>
	- jog(), as long as the hold action remains true (the key(s) remain pressed)
	jog() is called during mouse movement, here you can do the calculations for 
	analog type of actions.
	<br/>
	- finish_hold(), the return value isn't used at the moment.
	<br/>
	- prepare_actions(),if it returns -1, the InputEngine will recognise it as a
	failed command, and delete it.
	<br/>
	- do_action(), the actual action will happen here.
	

	For detailed information on how to use Command objects in Traverso, see InputEngine
 */




/**
 * 	Command constructor that only takes a description string.
	This type of Command doesn't know about a historystack
	So won't be un/redoable.
 * @param des The description of the action
 */
TCommand::TCommand( const QString& des )
    : TCommand(nullptr, des)
{
}

/**
 * 	Constructor with a ContextItem as parameter, which will
	be used to retrieve the historystack from. This Command tries
	to put itself on the ContextItem's history stack after the action
	is finished.
 * @param item The ContextItem this Command operates on
 * @param des  The description as will show up in the HistoryView
 */
TCommand::TCommand(ContextItem* item, const QString& des)
    : QUndoCommand(des)
{
    m_isValid = false;
    m_canvasCursorFollowsMouseCursor = true;

    if (item) {
        m_historyStack = item->get_history_stack();
    } else {
        m_historyStack = nullptr;
    }
}

TCommand::~TCommand()
= default;

/**
 * 	Virtual function, only needs to be reimplemented when making a 
	hold type of Command
	
	In case of a 'Hold fact' type of Command, you should also retrieve<br />
	the 'old' state here to be able to restore it in undo_action()
 
 * @return Return value must != -1 on success, -1 on failure
 */
int TCommand::begin_hold()
{
	PERROR("Hold actions should re-implement this function!!");
	return -1;
}

/**
 * 	Virtual function, only needs to be reimplemented when making a 
	hold type of Command
 * @return Return value not used right now
 */
int TCommand::finish_hold()
{
	return -1;
}

/**
 * 	Virtual function, only needs to be reimplemented when making a 
	hold type of Command

	This function makes it possible to create 'analog' type of actions possible.<br />
	Use the convenience functions of ContextPointer to get scene x and y coordinates<br />
	to move or adjust canvas items positions/parameters.

 * @return Return value not used right now
 */
int TCommand::jog()
{
	return -1;
}

/**
 * 	Used by the InputEngine to set a Command valid or not, which is
	detected by the return values of begin_hold(), prepare_actions()

	Only valid commands will be pushed upon the historystack. 
* @param valid 
 */
void TCommand::set_valid(bool valid)
{
	PENTER3;
	m_isValid = valid;
}

// Internal function, not to be used outside of TInputEventDispatcher
int TCommand::push_to_history_stack( )
{
    PENTER3;
	
	if (! m_isValid) {
        PMESG3("This command is invalid, deleting the command");
		return -1;
	}
	
	if (! m_historyStack) {
        PMESG3("This command has no HistoryStack, deleting the command");
		return -1;
	}
		
	m_historyStack->push(this);
	
	return 1;
}

/**
 * 	Virtual function, needs to be reimplemented for all
	type of Commands

	Use this function to do any kind of calculation or information gathering
	to be able to perform the actual action in do_action()
 * @return Return value must != -1 on success, -1 on failure
 */
int TCommand::prepare_actions( )
{
	return -1;
}

/**
 * 	Virtual function, needs to be reimplemented for all
	type of Commands

	This function is called after the action is finished and
	each time the historystack 'redoes' an action.<br />
	Normally the data created in prepare_actions() will be used
	here to do the actuall action, which most of the case will be 
	one or a few functions calls.

	In case of a Single fact type of Command, you should also retrieve<br />
	the 'old' state here to be able to restore it in undo_action()
 */
int TCommand::do_action( )
{
	return -1;
}

/**
 * 	Virtual function, needs to be reimplemented for all
	type of Commands

	This function is called  each time the historystack 'undoes' an action, <br />
	use prepare_actions(), or in case of a hold type command begin_hold() to <br />
	store the old value(s), your command will change in do_action(), and use <br />
	those here to restore the old state.
 */
int TCommand::undo_action( )
{
	return -1;
}

/**
 * 	Cancels the action (makes only sense for hold type of actions).
	Reimplement to undo any changes allready made, either be it 
	data from the core that has changed, or e.g. gui items who
	moved in the jog() function.
 */
void TCommand::cancel_action()
{
	// reimplement me
}

/**
 *  	Reimplement this function to receive the so called 'collected number' of the InputEngine
	
	The number collection is active during Hold Actions. A Hold type of command can use it 
	to let the user type in a numerical value, which is then parsed by the (derived) Command class
	to set the variable it controls, like the position of an AudioClip, or the gain value of a Track
 * @param collected The QString of the collected number so far. Note that it can include a . (period) and
			a , (comma), hence the number is supplied as a string, and not a numerical value.
 */
void TCommand::process_collected_number(const QString & collected)
{
	Q_UNUSED(collected);
	// reimplement me
}

/**
 * 	Uses the mouse hints specified in the keymap.xml file to set a cursor
	to hint the user which movement has to be made on hold type of commands

	Reimplement if you want a different type of cursor, or want to use the 
	more advanced HoldCursor supplied by ViewPort
 * @param useX If 1, suggests horizontal mouse movement
 * @param useY If 1, suggests vertical mouse movement
 */
void TCommand::set_cursor_shape( int useX, int useY )
{
	if (useX && useY) {
		 cpointer().set_canvas_cursor_shape(":/cursorHoldLrud");
	} else if (useX) {
		cpointer().set_canvas_cursor_shape(":/cursorHoldLr");
	} else if (useY) {
		cpointer().set_canvas_cursor_shape(":/cursorHoldUd");
	} else{
		cpointer().set_canvas_cursor_shape(":/cursorFloat");
	}
	
}

void TCommand::process_command(TCommand * cmd)
{
	Q_ASSERT(cmd);

    if (cmd->prepare_actions() != -1) {
		cmd->set_valid(true);
		if (cmd->push_to_history_stack() < 0) {
			// QUndoStack calls redo() for us, now it's not
			// called, so we do it here!
			cmd->redo();
			delete cmd;
		}
	}
}

void TCommand::set_do_not_push_to_historystack()
{
    m_historyStack = nullptr;
}

