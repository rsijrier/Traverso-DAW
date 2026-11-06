/*
    Copyright (C) 2010 - 2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TGain.h"
#include "TAudioProcessingNode.h"
#include <cmath>
#include "TAudioPluginChain.h"
#include "GainEnvelope.h"
#include "Debugger.h"

// A member of its owner, so it takes no QObject parent; the plugin chain is a
// child of the owner
TAudioProcessingNode::TAudioProcessingNode(TSession *session, TContextItem* owner)
    : m_pluginChain(new TAudioPluginChain(owner, session))
    , m_fader(m_pluginChain->get_fader())
{
    Q_ASSERT(owner);
    m_isMuted = false;
    m_pan = 0.0f;
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
    m_isMuted = muted;
    emit muteChanged(m_isMuted);
}

TCommand* TAudioProcessingNode::mute()
{
    PENTER;
    set_muted(!m_isMuted);
    return nullptr;
}

void TAudioProcessingNode::set_gain(float gain)
{
    m_fader->set_gain(TGain::clamp(gain));

    emit gainChanged();
}

float TAudioProcessingNode::get_gain() T_REALTIME_SAFE {
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
    gain = TGain::clamp(gain);

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
