/*
    Copyright (C) 2005-2024 Remon Sijrier

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

#include "TViewItem.h"

#include <QBrush>
#include <QPointer>
#include <QTimeLine>

class TSession;
class TSheetView;
class TClipsViewPort;
class TFrameClock;


class TPlayHeadView : public TViewItem
{
    Q_OBJECT

public:
    TPlayHeadView(TSheetView* sv, TSession* session, TClipsViewPort* vp, TFrameClock* frameClock);
    ~TPlayHeadView();

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    void set_bounding_rect(QRectF rect);

    bool is_active();
    void set_active(bool active);

    enum PlayHeadMode {
        FLIP_PAGE,
        CENTERED,
        ANIMATED_FLIP_PAGE
    };

    void set_mode(PlayHeadMode mode);
    void toggle_follow();

private:
    TSession*	m_session;
    QPointer<TFrameClock> m_frameClock;
    QTimeLine	m_animation;
    TClipsViewPort*	m_vp;
    bool 		m_follow{};
    bool		m_followDisabled{};
    PlayHeadMode	m_mode;
    int 		m_animationScrollStartPos{};
    qreal		m_animFrameRange{};
    qreal		m_animScaleFactor{};
    QBrush          m_brushActive;
    QBrush          m_brushInactive;
    QPixmap		m_pixActive;
    QPixmap		m_pixInActive;

    void create_pixmap();
private slots:
    void check_config();
    void play_start();
    void play_stop();
    void set_animation_value(int);
    void animation_finished();
    void load_theme_data();

public slots:
    // nowNanoseconds: the frame time, see TSession::get_heard_transport_location()
    void update_position(uint64_t nowNanoseconds);
    void enable_follow();  // enable/disable follow only do anything if following is
    void disable_follow(); // enabled in the config
};

