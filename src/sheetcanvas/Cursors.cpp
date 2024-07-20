/*
    Copyright (C) 2005-2007 Remon Sijrier 
 
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

#include "Cursors.h"
#include "SheetView.h"
#include "ClipsViewPort.h"
#include "AudioDevice.h"
#include <Sheet.h>
#include "TConfig.h"
#include <Themer.h>

#include <QPen>
#include <QScrollBar>
#include <QApplication>
		
// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

#define ANIME_DURATION		1000
#define AUTO_SCROLL_MARGIN	0.05  // autoscroll when within 5% of the clip view port


PlayHead::PlayHead(SheetView* sv, TSession* session, ClipsViewPort* vp)
        : ViewItem(nullptr, session)
        , m_session(session)
        , m_vp(vp)
        , m_mode(ANIMATED_FLIP_PAGE)
{
	m_sv = sv;
	check_config();
	connect(&(config()), SIGNAL(configChanged()), this, SLOT(check_config()));
	
	// TODO: Make duration scale with scalefactor? (nonlinerly?)
	m_animation.setDuration(ANIME_DURATION);
    m_animation.setEasingCurve(QEasingCurve::InOutQuad);
	
	connect(m_session, SIGNAL(transportStarted()), this, SLOT(play_start()));
	connect(m_session, SIGNAL(transportStopped()), this, SLOT(play_stop()));
	
	connect(&m_playTimer, SIGNAL(timeout()), this, SLOT(update_position()));
	
	connect(&m_animation, SIGNAL(frameChanged(int)), this, SLOT(set_animation_value(int)));
	connect(&m_animation, SIGNAL(finished()), this, SLOT(animation_finished()));
        connect(themer(), SIGNAL(themeLoaded()), this, SLOT(load_theme_data()), Qt::QueuedConnection);

    PlayHead::load_theme_data();

	setZValue(99);
}

PlayHead::~PlayHead( )
{
        PENTERDES2;
}

void PlayHead::check_config( )
{
    m_mode = static_cast<PlayHeadMode>(config().get_property("PlayHead", "Scrollmode", ANIMATED_FLIP_PAGE).toInt());
	m_follow = config().get_property("PlayHead", "Follow", true).toBool();
	m_followDisabled = false;
}

void PlayHead::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
	Q_UNUSED(option);
	Q_UNUSED(widget);

    if (m_pixActive.height() != int(m_boundingRect.height())) {
        create_pixmap();
    }

    painter->drawPixmap(0, 0, int(m_boundingRect.width()), int(m_boundingRect.height()), m_pixActive);
}


void PlayHead::create_pixmap()
{
    m_pixActive = QPixmap(int(m_boundingRect.width()), int(m_boundingRect.height()));
    m_pixActive.fill(Qt::transparent);

    QPainter p(&m_pixActive);
    p.fillRect(QRectF(0, 0, m_boundingRect.width() - 2, m_boundingRect.height()), m_brushActive);
}

void PlayHead::play_start()
{
	show();

	m_followDisabled = false;

	m_playTimer.start(20);
	
	if (m_animation.state() == QTimeLine::Running) {
		m_animation.stop();
		m_animation.setCurrentTime(0);
	}
}

void PlayHead::play_stop()
{
	m_playTimer.stop();

	if (m_animation.state() == QTimeLine::Running) {
		m_animation.stop();
	}
	
	// just one more update so the playhead can paint itself
	// in the correct color.
	update();

}

void PlayHead::disable_follow()
{
	m_followDisabled = true;
}

void PlayHead::enable_follow()
{
	m_followDisabled = false;
	// This function is called after the sheet finished a seek action.
	// if the sheet is still playing, update our position, and start moving again!
	if (m_session->is_transport_rolling()) {
		play_start();
	}
}

void PlayHead::update_position()
{
	QPointF newPos(m_session->get_transport_location() / m_sv->timeref_scalefactor, 1);
    qreal playBufferTimePositionCompensation = 0;
	if (m_session->is_transport_rolling()) {
        playBufferTimePositionCompensation = audiodevice().get_buffer_latency() / m_sv->timeref_scalefactor;
	}
	qreal newXPos = newPos.x() - playBufferTimePositionCompensation;
	if (newXPos < 0.0) {
		newXPos = 0.0;
	}
	newPos.setX(newXPos);
	
	
    if (int(newPos.x()) != int(pos().x()) && (m_animation.state() != QTimeLine::Running)) {
		setPos(newPos);
    } else {
        return;
    }

	if ( ! m_follow || m_followDisabled || ! m_session->is_transport_rolling()) {
		return;
	}
	
	int vpWidth = m_vp->viewport()->width();
	
	// When timeref_scalefactor is below 5120, the playhead moves faster then teh view scrolls
	// so it's better to keep the view centered around the playhead.
	if (m_mode == CENTERED || (m_sv->timeref_scalefactor <= 10280) ) {
                // For some reason on some systems the event of
                // setting the new position doesn't result in
                // updating the area of the old cursor position
                // when directly updating the scrollbars afterwards.
                // processing the event stack manually solves this.
                qApp->processEvents();

                m_sv->set_hscrollbar_value(int(scenePos().x()) - int(0.5 * vpWidth));
		return;
	}
	 
	QPoint vppoint = m_vp->mapFromScene(pos());
	
	if (vppoint.x() < 0 || (vppoint.x() > vpWidth)) {
		
		// If the playhead is _not_ in the viewports range, center it in the middle!
        m_sv->set_hscrollbar_value(int(scenePos().x()) - int(0.5 * vpWidth));
	
	} else if (vppoint.x() > ( vpWidth * (1.0 - AUTO_SCROLL_MARGIN) )) {
		
		// If the playhead is in the viewports range, and is nearing the end
		// either start the animated flip page, or flip the page and place the 
		// playhead cursor ~ 1/10 from the left viewport border
		if (m_mode == ANIMATED_FLIP_PAGE) {
			if (m_animation.state() != QTimeLine::Running) {
                m_animFrameRange = int(vpWidth * (1.0 - (AUTO_SCROLL_MARGIN * 2)));
                m_animation.setFrameRange(0, int(m_animFrameRange));
				m_animationScrollStartPos = m_sv->hscrollbar_value();
				m_animScaleFactor = m_sv->timeref_scalefactor;
				//during the animation, we stop the play update timer
				// to avoid unnecessary update/paint events
				play_stop();
				m_animation.setCurrentTime(0);
				m_animation.start();
			}
		} else {
            m_sv->set_hscrollbar_value(int(int(scenePos().x()) - (AUTO_SCROLL_MARGIN * vpWidth)) );
		}
	}
}

void PlayHead::set_animation_value(int /*value*/)
{
	// When the scalefactor changed, stop the animation here as it's no longer valid to run
	// and reset the animation timeline time back to 0.
    if (!qFuzzyCompare(m_animScaleFactor, m_sv->timeref_scalefactor)) {
		m_animation.stop();
		m_animation.setCurrentTime(0);
		animation_finished();
		return;
	}

	QPointF newPos(m_session->get_transport_location() / m_sv->timeref_scalefactor, 0);
	
	// calculate the animation x diff.
    qreal diff = m_animation.currentValue() * m_animFrameRange;
	
	// compensate for the playhead movement.
	m_animationScrollStartPos += newPos.x() - pos().x();
	
    int newXPos = int(m_animationScrollStartPos + diff);
	
	if (newPos != pos()) {
        setPos(newPos);
	}
	
	if (m_sv->hscrollbar_value() != newXPos) {
                // For some reason on some systems the event of
                // setting the new position doesn't result in
                // updating the area of the old cursor position
                // when directly updating the scrollbars afterwards.
                // processing the event stack manually solves this.
                qApp->processEvents();
		m_sv->set_hscrollbar_value(newXPos);
	}
}

void PlayHead::animation_finished()
{
	if (m_session->is_transport_rolling()) {
		play_start();
	}
}


void PlayHead::set_bounding_rect( QRectF rect )
{
	m_boundingRect = rect;
}

bool PlayHead::is_active()
{
	return m_playTimer.isActive();
}

void PlayHead::set_active(bool active)
{
	if (active) {
		play_start();
	} else {
		play_stop();
	}
}

void PlayHead::set_mode( PlayHeadMode mode )
{
	m_mode = mode;
}

void PlayHead::toggle_follow( )
{
	m_follow = ! m_follow;
}

void PlayHead::load_theme_data()
{
    m_brushActive = themer()->get_brush("Playhead:active");
    m_brushInactive = themer()->get_brush("Playhead:inactive");
}

/**************************************************************/
/*                    WorkCursor                              */
/**************************************************************/


WorkCursor::WorkCursor(SheetView* sv, TSession* session)
        : ViewItem(nullptr, session)
        , m_session(session)
	, m_sv(sv)
{
	setZValue(100);
}

WorkCursor::~WorkCursor( )
{
        PENTERDES2;
}

void WorkCursor::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
	Q_UNUSED(option);
	Q_UNUSED(widget);
	
	if (m_pix.height() != int(m_boundingRect.height())) {
		update_background();
	}
	
	painter->drawPixmap(0, 0, int(m_boundingRect.width()), int(m_boundingRect.height()), m_pix);
}

void WorkCursor::update_position()
{
	setPos(m_session->get_work_location() / m_sv->timeref_scalefactor, 1);
}

void WorkCursor::set_bounding_rect( QRectF rect )
{
	m_boundingRect = rect;
}

void WorkCursor::update_background()
{
	m_pix = QPixmap(int(m_boundingRect.width()), int(m_boundingRect.height()));
	m_pix.fill(Qt::transparent);
	QPainter p(&m_pix);
	QPen pen;
	pen.setWidth(4);
	pen.setStyle(Qt::DashDotLine);
	pen.setColor(themer()->get_color("Workcursor:default"));
	p.setPen(pen);
	p.drawLine(0, 0, int(m_boundingRect.width()), int(m_boundingRect.height()));
}



