/*
Copyright (C) 2019-2026 Remon Sijrier

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

#include "TFaderGroupCommand.h"
#include "TFaderCommand.h"
#include "TGain.h"
#include "TAudioProcessingNode.h"
#include "Debugger.h"
#include "TContextPointer.h"

TFaderGroupCommand::TFaderGroupCommand(TContextItem *context, TAudioProcessingNode* node, const QString& name)
    : TCommand (context)
    , m_node(node)
    , m_primaryGainOnly(false)
{
    Q_ASSERT(m_node);
    m_canvasCursorFollowsMouseCursor = false;
    setText("Gain: " + name);
}

TFaderGroupCommand::~TFaderGroupCommand()
{
    PENTERDES;
}

void TFaderGroupCommand::set_cursor_shape(int useX, int useY)
{
    Q_UNUSED(useX);
    Q_UNUSED(useY);
    m_contextPointer->set_canvas_cursor_type(TContextPointer::CursorType::GainFader);
}

int TFaderGroupCommand::begin_hold()
{
    m_origPos = m_contextPointer->scene_pos();

    float absoluteDb = TGain::to_dB(m_node->get_gain());
    m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(absoluteDb));

    return 1;
}

int TFaderGroupCommand::finish_hold()
{
    if (!m_pendingNumericalValue.isEmpty()) {
        bool ok;
        double dbValue = m_pendingNumericalValue.toDouble(&ok);
        if (ok) {
            float validatedDb = static_cast<float>(dbValue);
            float targetGain = (validatedDb <= -120.0f) ? 0.0f : TGain::from_dB(validatedDb);

            if (m_primaryGainOnly) {
                m_gainCommands.at(0)->set_new_gain(targetGain);
            } else {
                for (auto const &gain : m_gainCommands) {
                    gain->set_new_gain(targetGain);
                }
            }
        }
        m_pendingNumericalValue.clear();
    }
    return 1;
}


void TFaderGroupCommand::cancel_action()
{
    PENTER;
    m_pendingNumericalValue.clear();
    for (auto const &gain : m_gainCommands) {
        gain->cancel_action();
    }
}

void TFaderGroupCommand::process_collected_number(const QString &collected)
{
    Q_ASSERT(!m_gainCommands.empty());
    m_pendingNumericalValue = collected;

    if (collected.isEmpty()) {
        m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(TGain::to_dB(m_node->get_gain())));
        return;
    }

    bool ok;
    audio_sample_t dbFactor = audio_sample_t(collected.toDouble(&ok));
    QString displayString;

    // Process typed characters sequentially into formatted dB readout arrays
    if (!ok) {
        if (collected.contains(QStringLiteral(".")) || collected.contains(QStringLiteral("-"))) {
            displayString = collected + QStringLiteral(" dB");
        } else {
            displayString = collected;
        }
    } else {
        int rightfromdot = 0;
        if (collected.contains(QStringLiteral("."))) {
            rightfromdot = collected.size() - collected.lastIndexOf(QStringLiteral(".")) - 1;
        }
        displayString = rightfromdot ? QByteArray::number(double(dbFactor), 'f', rightfromdot).append(" dB")
                                     : QByteArray::number(double(dbFactor)).append(" dB");
    }

    m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(displayString));
}

int TFaderGroupCommand::jog()
{
    Q_ASSERT(!m_gainCommands.empty());
    qreal diff = m_origPos.y() - m_contextPointer->scene_y();

    if (m_primaryGainOnly) {
        m_gainCommands.at(0)->process_mouse_move(diff);
    } else {
        for (auto const &gain : m_gainCommands) {
            gain->process_mouse_move(diff);
        }
    }

    m_contextPointer->set_canvas_cursor_pos(m_origPos);

    m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(TGain::to_dB(m_node->get_gain())));

    return 1;
}

int TFaderGroupCommand::prepare_actions()
{
    if (m_gainCommands.empty()) {
        return -1;
    }

    for (auto const &gain : m_gainCommands) {
        if (gain->prepare_actions() == -1) {
            return -1;
        }
    }
    return 1;
}

int TFaderGroupCommand::do_action()
{
    Q_ASSERT(!m_gainCommands.empty());
    if (m_primaryGainOnly) {
        m_gainCommands.at(0)->do_action();
    } else {
        for (auto const &gain : m_gainCommands) {
            gain->do_action();
        }
    }
    return 1;
}

int TFaderGroupCommand::undo_action()
{
    Q_ASSERT(!m_gainCommands.empty());
    if (m_primaryGainOnly) {
        m_gainCommands.at(0)->undo_action();
    } else {
        for (auto const &gain : m_gainCommands) {
            gain->undo_action();
        }
    }
    return 1;
}

void TFaderGroupCommand::add_audio_processing_node(TContextItem* owner, TAudioProcessingNode* audioProcessingNode, const QVariantList& args)
{
    Q_ASSERT(owner && audioProcessingNode);
    m_gainCommands.push_back(std::make_unique<TFaderCommand>(owner, audioProcessingNode, args));
}

void TFaderGroupCommand::increase_gain()
{
    Q_ASSERT(!m_gainCommands.empty());

    if (m_primaryGainOnly) {
        m_gainCommands.at(0)->increase_gain();
    } else {
        for (auto const &gain : m_gainCommands) {
            gain->increase_gain();
        }
    }

    float absoluteDb = TGain::to_dB(m_node->get_gain());
    m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(static_cast<float>(absoluteDb)));
}

void TFaderGroupCommand::decrease_gain()
{
    Q_ASSERT(!m_gainCommands.empty());

    if (m_primaryGainOnly) {
        m_gainCommands.at(0)->decrease_gain();
    } else {
        for (auto const &gain : m_gainCommands) {
            gain->decrease_gain();
        }
    }

    m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(TGain::to_dB(m_node->get_gain())));
}

void TFaderGroupCommand::reset_gain()
{
    for (auto const &gain : m_gainCommands) {
        gain->set_new_gain(1.0f);
    }
    m_contextPointer->set_canvas_cursor_text("0.0 dB");
    m_contextPointer->set_canvas_cursor_data(QVariant::fromValue(0.0f));
}

void TFaderGroupCommand::toggle_primary_gain_only()
{
    if (m_gainCommands.size() == 1) {
        m_contextPointer->set_canvas_cursor_text(tr("Clip is not part of a selection..."), 2000);
        return;
    }
    m_primaryGainOnly = !m_primaryGainOnly;
    m_contextPointer->set_canvas_cursor_text(m_primaryGainOnly ? tr("To Selection: Off") : tr("To Selection: On"));
}

void TFaderGroupCommand::numerical_input()
{
    m_contextPointer->set_canvas_cursor_text(tr("Use numerical keys to set gain dB value..."), 2000);
}
