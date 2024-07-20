/*
Copyright (C) 2010 Remon Sijrier

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


#include "TAudioProcessingNode.h"

#include <cmath>

#include "AudioClip.h"
#include "PluginChain.h"
#include "TSession.h"


#include "Debugger.h"

TAudioProcessingNode::TAudioProcessingNode(TSession *session)
        : ContextItem(session)
        , m_session(session)
{
        if (m_session) {
                m_pluginChain = new PluginChain(this, m_session);
                set_history_stack(m_session->get_history_stack());
        } else {
                m_pluginChain = new PluginChain(this);
        }

        m_processBus = nullptr;
        m_isMuted = false;
        m_pan = 0.0f;
        m_maxGainAmplification = 2.0f;
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
        m_isMuted = muted;
        emit muteChanged(m_isMuted);
        emit audibleStateChanged();
}

TCommand* TAudioProcessingNode::mute()
{
        PENTER;
        set_muted(!m_isMuted);

        return nullptr;
}

void TAudioProcessingNode::set_gain(float gain)
{
    if (gain < 0.0f) {
            gain = 0.0;
    }
    if (gain > m_maxGainAmplification) {
            gain = m_maxGainAmplification;
    }

    m_fader->set_gain(gain);

    emit stateChanged();
}

TCommand* TAudioProcessingNode::add_plugin( Plugin * plugin )
{
        return m_pluginChain->add_plugin(plugin);
}

TCommand* TAudioProcessingNode::remove_plugin( Plugin * plugin )
{
        return m_pluginChain->remove_plugin(plugin);
}


void TAudioProcessingNode::set_gain_animated(float gain)
{
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
