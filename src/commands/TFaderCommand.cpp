/*
    Copyright (C) 2010-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TFaderCommand.h"
#include "TContextItem.h"
#include "TAudioProcessingNode.h"
#include "TFaderLaw.h"
#include "TGain.h"
#include "Utils.h"
#include "Debugger.h"
#include "TFloatCompare.h"

/**
 * @class TFaderCommand
 * @brief Manages the execution, tracking, and high-performance manipulation of an audio node's gain multiplier.
 */

TFaderCommand::TFaderCommand(TContextItem* owner, TAudioProcessingNode* node, const QVariantList& /*args*/)
    : TCommand(owner, "")
    , m_audioProcessingNode(node)
{
    m_newGain = m_origGain = m_audioProcessingNode->get_gain();

    float currentDb = TGain::to_dB(m_newGain);
    m_faderPosition = TFaderLaw::dB_to_position(currentDb);
}

TFaderCommand::~TFaderCommand()
{
    PENTERDES;
}

int TFaderCommand::prepare_actions()
{
    if (TFloatCompare::compare(m_origGain, m_newGain)) {
        return -1;
    }
    return 1;
}

void TFaderCommand::apply_new_gain_to_object(float newGain)
{
    // Validate and secure value distribution natively via the centralized single source of truth
    m_newGain = TGain::clamp(newGain);
    m_audioProcessingNode->set_gain(m_newGain);

    // Read back final engine state and seamlessly synchronize UI fader positioning tracking variables
    m_newGain = m_audioProcessingNode->get_gain();
    float acceptedDb = TGain::to_dB(m_newGain);
    m_faderPosition = TFaderLaw::dB_to_position(acceptedDb);
}

int TFaderCommand::do_action()
{
    PENTER;
    if (TFloatCompare::compare(m_newGain, m_audioProcessingNode->get_gain())) {
        return 1;
    }
    m_audioProcessingNode->set_gain_animated(m_newGain);
    return 1;
}

int TFaderCommand::undo_action()
{
    PENTER;
    m_audioProcessingNode->set_gain_animated(m_origGain);

    float undoneDb = TGain::to_dB(m_origGain);
    m_faderPosition = TFaderLaw::dB_to_position(undoneDb);

    return 1;
}

void TFaderCommand::cancel_action()
{
    undo_action();
}

void TFaderCommand::increase_gain()
{
    // Incremental step change mimicking keyboard command shift interactions
    m_faderPosition = std::clamp(m_faderPosition + 0.015f, 0.0f, 1.0f);

    m_newGain = TFaderLaw::position_to_gain(m_faderPosition);
    m_audioProcessingNode->set_gain(m_newGain);
    m_newGain = m_audioProcessingNode->get_gain();
}

void TFaderCommand::decrease_gain()
{
    // Decremental step change mimicking keyboard command shift interactions
    m_faderPosition = std::clamp(m_faderPosition - 0.015f, 0.0f, 1.0f);

    m_newGain = TFaderLaw::position_to_gain(m_faderPosition);
    m_audioProcessingNode->set_gain(m_newGain);
    m_newGain = m_audioProcessingNode->get_gain();
}

void TFaderCommand::set_new_gain(float newGain)
{
    apply_new_gain_to_object(newGain);
    do_action();
}

void TFaderCommand::set_new_gain_numerical_input(float newGain)
{
    apply_new_gain_to_object(newGain);
}

void TFaderCommand::set_target_gain(float gain)
{
    m_newGain = TGain::clamp(gain);
    m_faderPosition = TFaderLaw::dB_to_position(TGain::to_dB(m_newGain));
}

int TFaderCommand::process_mouse_move(qreal diffY)
{
#if defined(Q_OS_MAC)
    if (!TraversoDAW::Utils::can_set_mouse_pos()) {
        // Evaluate condition against an absolute linear silence coefficient baseline to bypass layout jump errors
        if (m_origGain == 0.0f) {
            m_faderPosition = 0.0f;
        } else {
            float currentDb = TGain::to_dB(m_origGain);
            m_faderPosition = TFaderLaw::dB_to_position(currentDb);
        }
    }
#endif

    // User-ergonomic mouse sensitivity mapping definition constant (independent from layout bounds)
    static const qreal MOUSE_TRAVEL_RANGE_IN_PIXELS = 500.0;

    // 1. Accumulate input pixel offsets linearly into the native layout coordinate environment
    qreal delta = diffY / MOUSE_TRAVEL_RANGE_IN_PIXELS;

    // Explicitly clamp the intermediate positioning logic to native float thresholds during hardware updates
    m_faderPosition = static_cast<float>(std::clamp(m_faderPosition + delta, 0.0, 1.0));

    // 2. Decode the linear tracking boundaries cleanly into downstream processing parameters
    float targetGain = TFaderLaw::position_to_gain(m_faderPosition);

    // 3. Directly push the validated configuration parameters seamlessly to the audio rendering layers
    m_newGain = targetGain;
    m_audioProcessingNode->set_gain(m_newGain);

    return 1;
}
