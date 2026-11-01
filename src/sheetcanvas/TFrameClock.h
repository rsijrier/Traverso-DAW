// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QObject>
#include <QSet>
#include <QTimer>

class QWidget;

/**
 * @brief One clock for everything in the GUI that follows the audio
 *        transport: it emits frame() at the display's frame rate, only
 *        while a follower holds it.
 *
 * Every frame hands the same time to all followers, so a playhead and a
 * time label that read the heard transport position with it agree (see
 * TSession::get_heard_transport_location()). A follower that wants fewer
 * updates skips frames itself. The frame rate is the refresh rate of the
 * window's screen when the clock starts. In QML the frames would come from
 * FrameAnimation instead of the timer.
 */
class TFrameClock : public QObject
{
    Q_OBJECT

public:
    explicit TFrameClock(QWidget* window);

    // The clock runs while at least one follower holds it; a follower
    // releases it before it is destroyed
    void hold(QObject* follower);
    void release(QObject* follower);
    bool is_held_by(QObject* follower) const { return m_followers.contains(follower); }

signals:
    void frame(uint64_t nowNanoseconds);

private:
    void tick();

    QWidget*       m_window;
    QTimer         m_timer;
    QSet<QObject*> m_followers;
    bool           m_ticking{false};
};
