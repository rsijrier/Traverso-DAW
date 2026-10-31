
#include "TPlayHeadView.h"
#include "TClipsViewPort.h"
#include "TSheetView.h"
#include "TFrameClock.h"
#include "TClock.h"

#include <TSheet.h>
#include <TThemer.h>
#include "TConfig.h"
#include "qapplication.h"

#include "Debugger.h"

#define ANIME_DURATION		1000
#define AUTO_SCROLL_MARGIN	0.05  // autoscroll when

TPlayHeadView::TPlayHeadView(TSheetView* sv, TSession* session, TClipsViewPort* vp, TFrameClock* frameClock)
        : TViewItem(nullptr, session)
        , m_session(session)
        , m_frameClock(frameClock)
        , m_vp(vp)
        , m_mode(ANIMATED_FLIP_PAGE)
{
	m_sv = sv;
	check_config();
	connect(&(config()), &TConfig::configChanged, this, &TPlayHeadView::check_config);
	
	// TODO: Make duration scale with scalefactor? (nonlinerly?)
	m_animation.setDuration(ANIME_DURATION);
    m_animation.setEasingCurve(QEasingCurve::InOutQuad);
	
	connect(m_session, &TSession::transportStarted, this, &TPlayHeadView::play_start);
	connect(m_session, &TSession::transportStopped, this, &TPlayHeadView::play_stop);
	
	// Every frame while the transport rolls
	// Only while this view holds the clock: another follower (the time
	// label) keeps it running while the playhead is grabbed (PlayHeadMove)
	connect(m_frameClock, &TFrameClock::frame, this, [this](uint64_t nowNanoseconds) {
		if (m_frameClock->is_held_by(this)) {
			update_position(nowNanoseconds);
		}
	});
	
	connect(&m_animation, &QTimeLine::frameChanged, this, &TPlayHeadView::set_animation_value);
	connect(&m_animation, &QTimeLine::finished, this, &TPlayHeadView::animation_finished);
        connect(themer(), &TThemer::themeLoaded, this, &TPlayHeadView::load_theme_data, Qt::QueuedConnection);

    TPlayHeadView::load_theme_data();

	setZValue(99);
}

TPlayHeadView::~TPlayHeadView( )
{
        PENTERDES2;

        if (m_frameClock) {
                m_frameClock->release(this);
        }
}

void TPlayHeadView::check_config( )
{
    m_mode = static_cast<PlayHeadMode>(config().get_property("PlayHead", "Scrollmode", ANIMATED_FLIP_PAGE).toInt());
	m_follow = config().get_property("PlayHead", "Follow", true).toBool();
	m_followDisabled = false;
}

void TPlayHeadView::paint( QPainter * painter, const QStyleOptionGraphicsItem * option, QWidget * widget )
{
	Q_UNUSED(option);
    Q_UNUSED(widget);

    if (m_pixActive.height() != int(m_boundingRect.height())) {
        create_pixmap();
    }

    painter->drawPixmap(0, 0, int(m_boundingRect.width()), int(m_boundingRect.height()), m_pixActive);
}

void TPlayHeadView::create_pixmap()
{
    m_pixActive = QPixmap(int(m_boundingRect.width()), int(m_boundingRect.height()));
    m_pixActive.fill(Qt::transparent);

    QPainter p(&m_pixActive);
    p.fillRect(QRectF(0, 0, m_boundingRect.width() - 2, m_boundingRect.height()), m_brushActive);
}

void TPlayHeadView::play_start()
{
	show();

	m_followDisabled = false;

	m_frameClock->hold(this);
	
	if (m_animation.state() == QTimeLine::Running) {
		m_animation.stop();
		m_animation.setCurrentTime(0);
	}
}

void TPlayHeadView::play_stop()
{
	m_frameClock->release(this);

	if (m_animation.state() == QTimeLine::Running) {
		m_animation.stop();
	}
	
	// just one more update so the playhead can paint itself
	// in the correct color.
	update();

	// Stopped: show where the transport is instead of where it was heard,
	// a latency behind. During the page flip it still rolls
	if (!m_session->is_transport_rolling()) {
        update_position(TClock::now_nanoseconds());
	}

}

void TPlayHeadView::disable_follow()
{
	m_followDisabled = true;
}

void TPlayHeadView::enable_follow()
{
	m_followDisabled = false;
	// This function is called after the sheet finished a seek action.
	// if the sheet is still playing, update our position, and start moving again!
	if (m_session->is_transport_rolling()) {
		play_start();
	}
}

void TPlayHeadView::update_position(uint64_t nowNanoseconds)
{
	// What is heard now: the output latency is accounted for
	QPointF newPos(m_session->get_heard_transport_location(nowNanoseconds) / m_sv->timeref_scalefactor, 1);
	if (newPos.x() < 0.0) {
		newPos.setX(0.0);
	}
	
	
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

void TPlayHeadView::set_animation_value(int /*value*/)
{
	// When the scalefactor changed, stop the animation here as it's no longer valid to run
	// and reset the animation timeline time back to 0.
    if (!TraversoDAW::Float::compare(m_animScaleFactor, m_sv->timeref_scalefactor)) {
		m_animation.stop();
		m_animation.setCurrentTime(0);
		animation_finished();
		return;
	}

    QPointF newPos(m_session->get_heard_transport_location(TClock::now_nanoseconds()) / m_sv->timeref_scalefactor, 0);
	
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

void TPlayHeadView::animation_finished()
{
	if (m_session->is_transport_rolling()) {
		play_start();
	}
}

void TPlayHeadView::set_bounding_rect( QRectF rect )
{
	m_boundingRect = rect;
}

bool TPlayHeadView::is_active()
{
	return m_frameClock->is_held_by(this);
}

void TPlayHeadView::set_active(bool active)
{
	if (active) {
		play_start();
	} else {
		play_stop();
	}
}

void TPlayHeadView::set_mode( PlayHeadMode mode )
{
	m_mode = mode;
}

void TPlayHeadView::toggle_follow( )
{
	m_follow = ! m_follow;
}

void TPlayHeadView::load_theme_data()
{
    m_brushActive = themer()->get_brush("Playhead:active");
    m_brushInactive = themer()->get_brush("Playhead:inactive");
}

