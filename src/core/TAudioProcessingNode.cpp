/*
    Copyright (C) 2010 - 2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TAudioProcessingNode.h"
#include <cmath>
#include "TAudioClip.h"
#include "TAudioPluginChain.h"
#include "TAudioThreadMessageQueue.h" // Integrated modernized post_rt_task
#include "TSession.h"
#include "TCurve.h" // Required to resolve the underlying fader-curve target memory
#include "GainEnvelope.h"
#include "Debugger.h"

TAudioProcessingNode::TAudioProcessingNode(TSession *session)
    : TContextItem(session)
    , m_session(session)
    , m_processBus(nullptr)
{
    if (m_session) {
        m_pluginChain = new TAudioPluginChain(this, m_session);
        set_history_stack(m_session->get_history_stack());
    } else {
        m_pluginChain = new TAudioPluginChain(this);
    }

    m_processBus = nullptr;
    m_isMuted = false;
    m_mutedBySolo = false;
    m_pan = 0.0f;
    m_fader = m_pluginChain->get_fader();
}

void TAudioProcessingNode::set_name( const QString & name )
{
    m_name = name;
    emit stateChanged();
}

void TAudioProcessingNode::set_pan(float pan)
{
    if ( pan < -1.0f ) {
        m_pan=-1.0;
    } else {
        if ( pan > 1.0f ) {
            m_pan=1.0;
        } else {
            m_pan=pan;
        }
    }

    if (std::fabs(pan) < std::numeric_limits<float>::epsilon()) {
        m_pan = 0.0f;
    }

    emit panChanged();
}

void TAudioProcessingNode::set_muted( bool muted )
{
    if (m_isMuted == muted) return;

    m_isMuted = muted;
    update_operational_gain();
}

void TAudioProcessingNode::set_muted_by_solo(bool mutedBySolo)
{
    if (m_mutedBySolo == mutedBySolo) return;

    m_mutedBySolo = mutedBySolo;
    update_operational_gain();
}

TCommand* TAudioProcessingNode::mute()
{
    PENTER;
    set_muted(!m_isMuted);
    return nullptr;
}

void TAudioProcessingNode::set_gain(float gain)
{
    m_gain = Mixer::clamp_gain(gain);
    if (m_fader) {
        m_fader->set_gain(m_gain); // Keep non-real-time persistence parameters aligned
    }

    update_operational_gain();
}

/**
 * @brief Centralized evaluation master router. Combines Gain, Mute, and Solo matrices,
 *        shipping the final single floating-point target safely into the TCurve real-time memory.
 */
void TAudioProcessingNode::update_operational_gain()
{
    float finalTargetGain = m_gain;

    // Hard-zero override: Explicit muting or solo isolation enforces immediate drop to zero
    if (m_isMuted || m_mutedBySolo) {
        finalTargetGain = 0.0f;
    }

    // Retrieve the target curve address from the fader plugin safely in the GUI context
    TCurve* faderCurve = m_fader ? m_fader->get_curve() : nullptr;

    // Dispatch the lock-free state-shipping transaction downstream
    tsmp().post_rt_task(
        // --- REAL-TIME AUDIO THREAD OPERATION ---
        [faderCurve, target = finalTargetGain]() {
            if (faderCurve) {
                // Securely transmit the target coefficient into the active curve tracking memory.
                // Executed strictly in-between audio blocks inside process_posted_gui_events().
                faderCurve->m_rtTargetMakeupGain = target;
            }
        },
        // --- MAIN GUI THREAD CALLBACK DEFERRAL ---
        [this]() {
            // Enforces clean interface metering alignment after hardware deployment has completed
            emit muteChanged(m_isMuted);
            emit soloMuteChanged(m_mutedBySolo);
            emit audibleStateChanged();
            emit stateChanged();
            emit gainChanged();
        }
        );
}

float TAudioProcessingNode::get_gain() {
    return m_fader ? m_fader->get_gain() : 1.0f;
}

QString TAudioProcessingNode::get_gain_db_string(int decimals)
{
    return m_fader ? m_fader->get_gain_db_string(decimals) : "0.0 dB";
}

TCommand* TAudioProcessingNode::add_plugin( TAudioPlugin * plugin )
{
    return m_pluginChain->add_plugin(plugin);
}

TCommand* TAudioProcessingNode::remove_plugin( TAudioPlugin * plugin )
{
    return m_pluginChain->remove_plugin(plugin);
}

void TAudioProcessingNode::set_gain_animated(float gain)
{
    gain = Mixer::clamp_gain(gain);

    if (m_gainAnimation.isNull()) {
        m_gainAnimation = new QPropertyAnimation(this, "gain");
    }

    if (m_gainAnimation->state() == QPropertyAnimation::Running) {
        m_gainAnimation->stop();
    }

    m_gainAnimation->setStartValue(get_gain());
    m_gainAnimation->setEndValue(gain);
    m_gainAnimation->setDuration(300);
    m_gainAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}
