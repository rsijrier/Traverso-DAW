/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "CommandGroup.h"
#include "TAudioThreadMessageQueue.h"
#include "TRtTransaction.h"
#include "Debugger.h"

CommandGroup::~CommandGroup()
{
    for (TCommand* cmd : m_commands) {
        delete cmd;
    }
}

int CommandGroup::prepare_actions()
{
    if (m_commands.empty()) {
        return -1;
    }

    int result = 1;
    for (TCommand* cmd : m_commands) {
        if (cmd->prepare_actions() == -1) {
            PWARN("CommandGroup::prepare_actions: one of the commands in the group failed prepare_actions\n");
            result = -1;
        }
    }

    return result;
}

int CommandGroup::do_action()
{
    // All real-time changes of the child commands reach the audio thread as one event
    TRtTransaction transaction;

    // Lets views suspend updates while the child commands run
    tsmp().trigger_batch_start();

    for (TCommand* cmd : m_commands) {
        cmd->do_action();
    }

    tsmp().trigger_batch_finish();

    return 1;
}

int CommandGroup::undo_action()
{
    TRtTransaction transaction;

    tsmp().trigger_batch_start();

    // Undo in reverse order
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it) {
        (*it)->undo_action();
    }

    tsmp().trigger_batch_finish();

    return 1;
}

