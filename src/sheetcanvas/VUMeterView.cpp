/*
    Copyright (C) 2005-2010 Remon Sijrier, Nicola Doebelin

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

#include "VUMeterView.h"

#include <QPainter>
#include <QGradient>

#include "AudioChannel.h"
#include "TConfig.h"
#include "Themer.h"
#include "Mixer.h"
#include <AudioDevice.h>
#include "Track.h"
#include "AudioTrack.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

/**
 * \class VUMeterView
 * \brief A widget holding level indicators, 'over' LEDs, and a scale
 *
 * The VUMeterView class is a widget that holds one or more level indicators (VUMeterLevelView),
 * one or more 'over' LEDs (VUMeterViewOverLedView), and one scale (VUMeterRulerView). Usually one
 * VUMeterView per track is created, which contains one level indicator and 'over' LED per
 * audio channel (2 for stereo) and one scale. The name of the track is shown below
 * the level indicators.
 *
 * The scale is hidden automatically if the width of the widget becomes too narrow.
 *
 * All VUMeterView related classes are designed to be arranged in a QGridLayout so as to
 * resize correctly.
 */


// initialize static variables
QVector<float> VUMeterView::lut;

VUMeterView::VUMeterView(ViewItem* parent, Track* track)
        : ViewItem(parent)
{
    VUMeterView::load_theme_data();
    m_audioTrack = qobject_cast<AudioTrack*>(track);

    for (int i = 0; i < 2; ++i) {
            VUMeterLevelView* level = new VUMeterLevelView(this, track->get_vumonitors().at(i));
            m_levels.append(level);
    }

//        add a ruler with tickmarks and labels
//        ruler = new VUMeterRulerView(this);
//        ruler->setPos(0, 10);

    connect(themer(), SIGNAL(themeLoaded()), this, SLOT(load_theme_data()), Qt::QueuedConnection);
    if (m_audioTrack) {
        connect(m_audioTrack, SIGNAL(armedChanged(bool)), this, SLOT(audiotrack_armed_changed()));
    }
}

VUMeterView::~ VUMeterView( )
= default;


void VUMeterView::paint(QPainter *painter, const QStyleOptionGraphicsItem */*option*/, QWidget */*widget*/)
{
        PENTER3;

        painter->save();
	QPen pen(themer()->get_color("VUMeter:levelseparator"));
	pen.setWidth(m_vulevelspacing);
	painter->setPen(pen);
        if (m_orientation == Qt::Vertical) {
                int center = qRound(m_boundingRect.width() / 2);
                painter->drawLine(center, 0, center, int(m_boundingRect.height()));
        } else {
		int center = qRound(m_boundingRect.height() / 2);
		painter->drawLine(2, center, int(m_boundingRect.width()) - 4, center);
        }

	painter->setRenderHint(QPainter::Antialiasing);
	painter->setBrush(QColor(0, 0, 0, 250));
	painter->setPen(Qt::NoPen);
    painter->drawRect(m_boundingRect);
    if (m_audioTrack && m_audioTrack->armed()) {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setBrush(Qt::transparent);
        QColor color = themer()->get_color("TrackPanel:recled");
        color.setAlpha(100);
        QPen armedPen;
        armedPen.setColor(color);
        armedPen.setWidth(2);
        painter->setPen(armedPen);
        painter->drawRect(m_boundingRect);
    }
    painter->restore();
}

void VUMeterView::calculate_bounding_rect()
{
        ViewItem::calculate_bounding_rect();
}

void VUMeterView::set_bounding_rect(QRectF rect)
{
        m_boundingRect = rect;
        int vertPos = 0;
        int horizontalPos = 0;
    m_vulevelspacing = 2;
        foreach(VUMeterLevelView* level, m_levels) {
                if (m_orientation == Qt::Vertical) {
                        level->set_bounding_rect(QRectF(0, 0, m_boundingRect.width() / m_levels.size(), m_boundingRect.height()));
                        level->setPos(horizontalPos, 0);
                        horizontalPos += level->boundingRect().width() + m_vulevelspacing;
                } else {
			level->set_bounding_rect(QRectF(0, 0, m_boundingRect.width() - 12, (m_boundingRect.height() / m_levels.size()) - 3));
			level->setPos(6, vertPos + 2);
                        vertPos += level->boundingRect().height() + m_vulevelspacing;
                }
        }

//        ruler->set_bounding_rect(rect);
}

void VUMeterView::update_orientation(Qt::Orientation orientation)
{
    m_orientation = orientation;
    foreach(VUMeterLevelView* level, m_levels) {
            level->set_orientation(orientation);
    }
}

void VUMeterView::calculate_lut_data()
{
        for (int i = 60; i >= -700; i -= 2) {
                if (i >= -200) {
                        lut.push_back(100.0f + i * 2.5f / 10.0f);
                } else if (i >= -300) {
                        lut.push_back( 50.0f + (i+200) * 2.0f / 10.0f);
                } else if (i >= -400) {
                        lut.push_back( 30.0f + (i+300) * 1.5f / 10.0f);
                } else if (i >= -500) {
                        lut.push_back( 15.0f + (i+400) * 0.75f / 10.0f);
                } else {
                        lut.push_back(  7.5f + (i+500) * 0.5f / 10.0f);
                }
        }
}

void VUMeterView::reset()
{
        foreach(VUMeterLevelView* level, m_levels) {
                level->reset();
        }
}

void VUMeterView::load_theme_data()
{
        m_vulevelspacing = themer()->get_property("VUMeterView:layout:vuspacing", 1).toInt();
        m_widgetBgBrush = themer()->get_brush("VUMeter:background:widget");
}

void VUMeterView::audiotrack_armed_changed()
{
    update();
}

/**********************************************************************/
/*                      VUMeterRulerView                                  */
/**********************************************************************/


/**
 * \class VUMeterRulerView
 * \brief A scale which places tick marks and labels from +6.0 to -60.0 dB
 *
 * The scale is compliant with IEC standard 60268-18 and uses VUMeterView::VUMeterView_lut()
 * to map dB values to widget positions.
 */

static const int TICK_LINE_LENGTH	= 2;
static const float LUT_MULTIPLY		= 5.0;

VUMeterRulerView::VUMeterRulerView(ViewItem* parent)
        : ViewItem(parent)
{
        m_boundingRect = parent->boundingRect();

        // labels
        m_presetMark.push_back(6);
        m_presetMark.push_back(0);
        m_presetMark.push_back(-6);
        m_presetMark.push_back(-12);
        m_presetMark.push_back(-24);
        m_presetMark.push_back(-70);

        VUMeterRulerView::load_theme_data();
        connect(themer(), SIGNAL(themeLoaded()), this, SLOT(load_theme_data()), Qt::QueuedConnection);
}

void VUMeterRulerView::paint(QPainter *painter, const QStyleOptionGraphicsItem */*option*/, QWidget */*widget*/)
{
    PENTER4;
    painter->save();

    int deltaY;

    painter->setFont(m_font);

    // offset is the space occupied by the 'over' LED
    qreal levelRange = m_boundingRect.width();

    painter->setPen(m_colorActive);

    // draw the labels
    for (uint j = 0; j < m_presetMark.size(); ++j) {

        int idx = int(LUT_MULTIPLY * float(-m_presetMark[j] + 6));

        // check the LUT index (I had exceptions without that check)
        if ((idx < 0) || (idx >= VUMeterView::VUMeterView_lut()->size())) {
            continue;
        }

        deltaY = int( VUMeterView::VUMeterView_lut()->at(idx)/115.0  * levelRange );
        QString spm("%1");
        spm = spm.arg(m_presetMark[j], 2, 10, QLatin1Char('0'));


        painter->drawText(deltaY - m_fontLabelAscent + 2, m_fontLabelAscent + 3, "spm");
        painter->drawLine(deltaY, - 6, deltaY, TICK_LINE_LENGTH - 6);
    }
    painter->restore();
}

void VUMeterRulerView::load_theme_data()
{
        m_font = themer()->get_font("VUMeter:fontscale:label");
        QFontMetrics fm(m_font);
        m_fontLabelAscent = fm.ascent();

        int gray = 70;
        m_colorActive = QColor(gray, gray, gray);//themer()->get_color("VUMeterView:font:active");
        m_colorInactive = themer()->get_color("VUMeterView:font:inactive");
}

void VUMeterRulerView::set_bounding_rect(QRectF rect)
{
        m_boundingRect = rect;
}


/**********************************************************************/
/*                      VUMeterLevelView                                    */
/**********************************************************************/


/**
 * \class VUMeterLevelView
 * \brief An audio level indicator widget
 *
 * This is a digital audio level meter which is compliant with the IEC standard
 * 60268-18. It ranges from +6.0 dB to -70.0 dB.
 *
 * A VUMeterLevelView is usually constructed within a VUMeterView object. The audio level is
 * read from an AudioChannel object, which must be given in the constructor. The
 * VUMeterLevelView is optimized for efficient space usage and switches from 3D look
 * to 2D look for narrow sizes.
 */

static const int OVER_SAMPLES_COUNT = 2;	// sensitivity of the 'over' indicator
static const int RMS_SAMPLES = 50;		// number of updates to be stored for RMS calculation
//static const int PEAK_HOLD_TIME = 1000;		// peak hold time (ms)
static const int PEAK_HOLD_MODE = 1;		// 0 = no peak hold, 1 = dynamic, 2 = constant
static const bool SHOW_RMS = false;		// toggle RMS lines on / off


VUMeterLevelView::VUMeterLevelView(ViewItem* parent, TVUMonitor* monitor)
        : ViewItem(parent)
{
        m_monitor = monitor;

	m_boundingRect = QRectF(0, 0, parent->boundingRect().width(), 5);
        m_tailDeltaY = m_peakHoldValue = m_rms = -120.0;
        m_overCount = m_rmsIndex = 0;
        m_peakHoldFalling = false;
        m_peak = 0.0;
        m_orientation = Qt::Vertical;

        // falloff speed, according to IEC 60268-18: 20 dB in 1.7 sec.
        m_maxFalloff = 20.0f / (1700.0f / float(TMainWindow::instance()->get_vulevel_update_frequency()));

        for (int i = 0; i < RMS_SAMPLES; i++) {
                m_peakHistory[i] = 0.0;
        }

        connect(themer(), SIGNAL(themeLoaded()), this, SLOT(load_theme_data()), Qt::QueuedConnection);
        load_theme_data();

        TMainWindow::instance()->register_vumeter_level(this);
}

VUMeterLevelView::~VUMeterLevelView()
{
        TMainWindow::instance()->unregister_vumeter_level(this);
}

void VUMeterLevelView::paint(QPainter* painter, const QStyleOptionGraphicsItem */*option*/, QWidget */*widget*/)
{
    PENTER4;
    painter->save();
    if (m_levelPixmap.width() != int(m_boundingRect.width())
            || m_levelPixmap.height() != int(m_boundingRect.height())) {
        resize_level_pixmap();
    }

    // convert the peak value to dB and make sure it's in a valid range
    float dBVal = float(coefficient_to_dB(m_peak));
    if (dBVal > 6.0f) {
        dBVal = 6.0f;
    }

    // calculate smooth falloff
    if (m_tailDeltaY - dBVal > m_maxFalloff) {
        dBVal = m_tailDeltaY - m_maxFalloff;
        m_tailDeltaY -= m_maxFalloff;
    } else {
        m_tailDeltaY = dBVal;
    }

    // check for a new peak hold value
    if (m_peakHoldFalling && (m_peakHoldValue >= -120.0f)) {
        // smooth falloff, a little faster than the level meter, looks better
        m_peakHoldValue -= 1.2f * m_maxFalloff;
    }

    if (m_peakHoldValue <= dBVal) {
        m_peakHoldFalling = false;
        m_peakHoldValue = dBVal;

        // ehm, oops, this timer was moved to Interface
        if (PEAK_HOLD_MODE == 1) {
            //                        phTimer.start(PEAK_HOLD_TIME);
        }
    }

    // convert dB values into widget position
    int meterLevel = get_meter_position(dBVal);
    int rmsLevel = get_meter_position(coefficient_to_dB(m_rms));
    int peakHoldLevel = get_meter_position(m_peakHoldValue);

    // draw levels
    //        painter->drawPixmap(0, 0, clearPixmap, 0, 0, meterLevel, -m_boundingRect.height());

    if (meterLevel > 0) {
        if (m_orientation == Qt::Horizontal) {
            painter->drawPixmap(0, 0, m_levelPixmap, 0, 0, meterLevel, int(m_boundingRect.height()));
        } else {
            painter->drawPixmap(0, meterLevel, m_levelPixmap, 0, meterLevel, int(m_boundingRect.width()), int(m_boundingRect.height()) - meterLevel);
        }
    }

    // draw RMS lines
    if (SHOW_RMS) {
        painter->setPen(Qt::blue);
        if (m_orientation == Qt::Horizontal) {
            painter->drawLine(rmsLevel, 0, rmsLevel, int(m_boundingRect.height()) - 1);
        } else {
            painter->drawLine(2, rmsLevel, int(m_boundingRect.width()) - 3, rmsLevel);
        }
    }

    // draw Peak hold lines
    if (PEAK_HOLD_MODE) {
        painter->setPen(m_colOverLed);
        if (m_orientation == Qt::Horizontal) {
            painter->drawLine(peakHoldLevel, 0, peakHoldLevel, int(m_boundingRect.height() - 1));
        } else {
            painter->drawLine(0, peakHoldLevel, int(m_boundingRect.width() - 1), peakHoldLevel);
        }
    }
    painter->restore();
}

void VUMeterLevelView::resize_level_pixmap( )
{
        m_levelPixmap = QPixmap(int(m_boundingRect.width()), int(m_boundingRect.height()));
        QPainter painter(&m_levelPixmap);

        if (m_orientation == Qt::Horizontal) {
                m_gradient2D.setStart(QPointF(m_boundingRect.width(), 0));
        } else {
                m_gradient2D.setFinalStop(QPointF(0.0, m_boundingRect.height()));
        }

        painter.fillRect(m_boundingRect, m_gradient2D);
        painter.end();

        m_clearPixmap = QPixmap(int(m_boundingRect.width()), int(m_boundingRect.height()));
        if (m_orientation == Qt::Horizontal) {
                m_levelClearColor = themer()->get_brush("VUMeter:background:bar", QPoint(0, 0), QPoint(int(m_boundingRect.width()), int(m_boundingRect.height())));
        } else {
                m_levelClearColor  = themer()->get_brush("VUMeter:background:bar", QPoint(0, 0), QPoint(0, int(m_boundingRect.height())));
        }
        painter.begin(&m_clearPixmap);
        painter.fillRect(m_boundingRect, m_levelClearColor);
}

void VUMeterLevelView::set_orientation(Qt::Orientation orientation)
{
        m_orientation = orientation;
        m_levelPixmap = QPixmap();
}

void VUMeterLevelView::update_peak( )
{
    //FIXME
    // We use a timer in TMainWindow to call this function at 25 frames/sec
    // What about TVUMonitor does the calculation after having process x amount of
    // frames and then emit the new peak value instead ?
        m_peak = m_monitor->get_peak_value();

        // if the meter drops to -inf, reset the 'over LED' and peak hold values
        if (qFuzzyCompare(m_peak, 0.0f) && (m_tailDeltaY <= -70.0f)) {
                m_peakHoldValue = -120.0;
//                emit activate_over_led(false);
                return;
        }

        // RMS lines
        if (SHOW_RMS) {
                // use some kind of 'ring buffer' to store the last couple of peak values
                if (m_rmsIndex >= RMS_SAMPLES) {
                        m_rmsIndex = 0;
                }

                m_peakHistory[m_rmsIndex] = m_peak;

                // calculate the RMS
                float squares = 0.0;

                for (int i = 0; i < RMS_SAMPLES; i++) {
                        squares += m_peakHistory[i] * m_peakHistory[i];
                }

                m_rms = float(sqrtf(squares / RMS_SAMPLES));
                m_rmsIndex++;
        }

        // 'over' detection
        if (m_peak >= 1.0f) m_overCount++;
        if (m_peak <  1.0f) m_overCount = 0.0f;

        if (m_overCount >= OVER_SAMPLES_COUNT) {
                emit activate_over_led(true);
                m_overCount = 0;
        }

        update(m_boundingRect);
}

void VUMeterLevelView::calculate_bounding_rect()
{
}

void VUMeterLevelView::set_bounding_rect(QRectF rect)
{
        m_boundingRect = rect;
        calculate_bounding_rect();
}


void VUMeterLevelView::reset_peak_hold_value()
{
        m_peakHoldFalling = true;
}

void VUMeterLevelView::load_theme_data()
{
        qreal zeroDB = 1.0 - 100.0/115.0;  // 0 dB position
        qreal msixDB = 1.0 -  80.0/115.0;  // -6 dB position
        qreal smooth = 0.05;

        m_gradient2D.setStart(0,0);

        m_gradient2D.setColorAt(0.0,           themer()->get_color("VUMeter:foreground:6db"));
        m_gradient2D.setColorAt(zeroDB-smooth, themer()->get_color("VUMeter:foreground:6db"));
        m_gradient2D.setColorAt(zeroDB+smooth, themer()->get_color("VUMeter:foreground:0db"));
        m_gradient2D.setColorAt(msixDB-smooth, themer()->get_color("VUMeter:foreground:0db"));
        m_gradient2D.setColorAt(msixDB+smooth, themer()->get_color("VUMeter:foreground:-6db"));
        m_gradient2D.setColorAt(1.0,           themer()->get_color("VUMeter:foreground:-60db"));

        m_levelClearColor  = themer()->get_brush("VUMeter:background:bar", QPoint(0, 0), QPoint(0, int(m_boundingRect.height())));
        m_colOverLed = themer()->get_color("VUMeter:overled:active");
        m_colBg = themer()->get_brush("VUMeter:background:bar");
}

// accepts dB-values and returns the position in the widget from top
int VUMeterLevelView::get_meter_position(float f)
{
        int idx = int(LUT_MULTIPLY * (-f + 6.0f));

        // Comment by Remon: If it happens, then it's a coding error?
        // if so, it could be an idea to hard check for this by using:
        // Q_ASSERT(idx < 0) (iirc) or, using something like a
        // qDebug("....") thing. Just an idea...

        // Reply by Nic: It's not a coding error, it happens for f > +6.0.
        //	The following part clips the value to +6.0, which is the
        //	highest value the LUT contains. I changed the comment accordingly.

        // clipping to the highest value covered by the LUT (+6.0 dB)
        if (idx < 0) {
                idx = 0;
        }

        // if idx > size of the LUT, dBVal is somewhere < -70 dB, which is not displayed
        if (idx >= VUMeterView::VUMeterView_lut()->size()) {
                if (m_orientation == Qt::Horizontal) {
                        return 0;
                } else {
                        return int(m_boundingRect.height());
                }
        } else {
                if (m_orientation == Qt::Horizontal) {
                        return  int(VUMeterView::VUMeterView_lut()->at(idx)/115.0f * float(m_boundingRect.width()));
                } else {
                        return  int(m_boundingRect.height() - int(VUMeterView::VUMeterView_lut()->at(idx)/115.0f * float(m_boundingRect.height())));
                }
        }
}


void VUMeterLevelView::reset()
{
        m_tailDeltaY = -120.0;
        m_peakHoldValue = -120.0;
        m_overCount = 0;
        emit activate_over_led(false);
        m_peak = 0;
        update();
}


