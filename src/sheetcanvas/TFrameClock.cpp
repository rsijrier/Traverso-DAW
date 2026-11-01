// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TFrameClock.h"
#include "TClock.h"

#include <QScreen>
#include <QWidget>

TFrameClock::TFrameClock(QWidget* window)
    : QObject(window)
    , m_window(window)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &TFrameClock::tick);
}

// Runs at the refresh rate of the window's screen at the start
void TFrameClock::hold(QObject* follower)
{
    m_followers.insert(follower);
    if (m_timer.isActive()) {
        return;
    }

    qreal refreshRate = 60.0;
    if (QScreen* screen = m_window->screen()) {
        if (screen->refreshRate() > 0) {
            refreshRate = screen->refreshRate();
        }
    }
    m_timer.start(int(1000.0 / refreshRate));
}

void TFrameClock::release(QObject* follower)
{
    m_followers.remove(follower);
    if (m_followers.isEmpty()) {
        m_timer.stop();
    }
}

void TFrameClock::tick()
{
    // A follower that processes events can make the timer fire again
    // inside its own frame: one frame at a time
    if (m_ticking) {
        return;
    }
    m_ticking = true;
    emit frame(TClock::now_nanoseconds());
    m_ticking = false;
}
