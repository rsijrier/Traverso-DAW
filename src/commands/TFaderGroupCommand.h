/*
    Copyright (C) 2019 - 2026 Remon Sijrier

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

#pragma once

#include "TCommand.h"
#include <QPointF>
#include <QString>
#include <vector>

class TFaderCommand;
class TAudioProcessingNode;

/**
 * @class TFaderGroupCommand
 * @brief Multi-channel group controller responsible for orchestrating individual gain commands and handling alphanumeric inputs safely.
 */
class TFaderGroupCommand : public TCommand
{
    Q_OBJECT

public:
    // context is the clip or track the gain is asked on, node its audio, name
    // its name for the undo text
    TFaderGroupCommand(TContextItem* context, TAudioProcessingNode* node, const QString& name);
    virtual ~TFaderGroupCommand() override;

    int begin_hold() override;
    int finish_hold() override;
    void cancel_action() override;
    void process_collected_number(const QString & collected) override;
    void set_cursor_shape(int useX, int useY) override;

    int jog() override;

    bool is_hold_command() const override { return true; }
    int prepare_actions() override;
    int do_action() override;
    int undo_action() override;

    bool wants_cursor_position_to_be_restored() const override { return true; }

    // One more gain to change: owner is its clip or track, for the undo stack
    void add_audio_processing_node(TContextItem* owner, TAudioProcessingNode* audioProcessingNode, const QVariantList& args);

private:
    std::vector<std::unique_ptr<TFaderCommand>> m_gainCommands; // Fixed unique_ptr copy-on-write crash by migration to std::vector
    QPointF                                   m_origPos;
    TAudioProcessingNode*                     m_node;
    bool                                      m_primaryGainOnly;
    QString                                   m_pendingNumericalValue; // Safely buffers user keystrokes during text processing

public slots:
    void increase_gain();
    void decrease_gain();
    void reset_gain();
    void toggle_primary_gain_only();
    void numerical_input();
};

