/*
Copyright (C) 2005-2009 Remon Sijrier

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

#include <QPainter>
#include <QFont>
#include <QGraphicsSimpleTextItem>

#include "AudioClipView.h"
#include "SheetView.h"
#include "AudioTrackView.h"
#include "FadeCurveView.h"
#include "CurveView.h"

#include "AudioClip.h"
#include "ReadSource.h"
#include "TInputEventDispatcher.h"
#include "ContextPointer.h"
#include "Sheet.h"
#include "ResourcesManager.h"
#include "ProjectManager.h"
#include "Peak.h"
#include "Information.h"
#include "Themer.h"
#include "TConfig.h"
#include <FadeCurve.h>
#include <Curve.h>
#include "TMainWindow.h"
#include "PluginChain.h"
#include "Mixer.h"

#include <QFileDialog>
#include <QLinearGradient>
#include <cmath>
#include "dialogs/AudioClipEditDialog.h"
#include "Fade.h"

#include "TTrackLaneView.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"


AudioClipView::AudioClipView(SheetView* sv, AudioTrackView* parent, AudioClip* clip )
    : ViewItem(parent->get_primary_lane_view(), clip)
    , m_tv(parent)
    , m_clip(clip)
{
    PENTERCONS;

    setZValue(parent->zValue() + 1);
    setFlags(QGraphicsItem::ItemUsesExtendedStyleOption);

    m_sv = sv;
    m_sheet = m_clip->get_sheet();

    AudioClipView::load_theme_data();

    m_waitingForPeaks = false;
    m_progress = 0;

    if (FadeCurve* curve = m_clip->get_fade_in()) {
        add_new_fade_curve_view(curve);
    }
    if (FadeCurve* curve = m_clip->get_fade_out()) {
        add_new_fade_curve_view(curve);
    }

    m_gainCurveView = new CurveView(m_sv, this, m_clip->get_plugin_chain()->get_fader()->get_curve());
    // CurveViews don't 'get' their start offset, it's only a property for AudioClips..
    // So to be sure the CurveNodeViews start offset get updated as well,
    // we call curveviews calculate_bounding_rect() function!
    m_gainCurveView->set_start_offset(m_clip->get_source_start_location());
    connect(m_gainCurveView, SIGNAL(curveModified()), m_sv, SLOT(stop_follow_play_head()));

    connect(m_clip, SIGNAL(muteChanged()), this, SLOT(repaint()));
    connect(m_clip, SIGNAL(stateChanged()), this, SLOT(clip_state_changed()));
    connect(m_clip, SIGNAL(activeContextChanged()), this, SLOT(active_context_changed()));
    connect(m_clip, SIGNAL(lockChanged()), this, SLOT(repaint()));
    connect(m_clip, SIGNAL(fadeAdded(FadeCurve*)), this, SLOT(add_new_fade_curve_view(FadeCurve*)));
    connect(m_clip, SIGNAL(fadeRemoved(FadeCurve*)), this, SLOT(remove_fade_curve_view(FadeCurve*)));
    connect(m_clip, SIGNAL(positionChanged()), this, SLOT(position_changed()));

    if (m_clip->recording_state() == AudioClip::RECORDING) {
        start_recording();
        connect(m_clip, SIGNAL(recordingFinished(AudioClip*)), this, SLOT(finish_recording()));
    }
}

AudioClipView::~ AudioClipView()
{
    PENTERDES;
}

void AudioClipView::paint(QPainter* painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    PENTER2;
    Q_UNUSED(widget);

    //        printf("AudioClipView:: %s PAINT :: exposed rect is: x=%f, y=%f,? w=%f, h=%f\n", QS_C(m_clip->get_name()), option->exposedRect.x(), option->exposedRect.y(), option->exposedRect.width(), option->exposedRect.height());

    //	printf("x, w %f, %f\n", option->exposedRect.x(), option->exposedRect.width());
    qreal xstart = option->exposedRect.x();
    qreal pixelcount = option->exposedRect.width();
    if (pixelcount < 0.5) {
        // apparently this function can be called with no pixelcount to go with
        // so return here nothing to be done
        return;
    }
    painter->save();
    painter->setClipRect(m_boundingRect);

    QRectF fillRect = QRectF(xstart, 0.0, pixelcount, qreal(m_height));

    if (m_clip->is_readsource_invalid()) {
        painter->fillRect(fillRect, themer()->get_color("AudioClip:invalidreadsource"));
        draw_clipinfo_area(painter, xstart);
        painter->setPen(themer()->get_color("AudioClip:contour"));
        painter->drawRect(xstart, 0, pixelcount, m_height - 1);
        painter->setPen(Qt::black);
        painter->setFont( themer()->get_font("AudioClip:fontscale:title") );
        painter->drawText(30, 0, 300, m_height, Qt::AlignVCenter, tr("Click to reset AudioFile !"));
        painter->restore();
        return;
    }

    bool mousehover = m_clip->has_active_context() || m_clip->is_moving();

    if (m_drawbackground) {
        if (m_clip->recording_state() == AudioClip::RECORDING) {
            painter->fillRect(fillRect, m_brushBgRecording);
        } else {
            if (m_clip->is_selected()) {
                if (mousehover) painter->fillRect(fillRect, m_brushBgSelectedHover);
                else            painter->fillRect(fillRect, m_brushBgSelected);
            } else if (m_clip->is_muted()) {
                if (mousehover) painter->fillRect(fillRect, m_brushBgMutedHover);
                else            painter->fillRect(fillRect, m_brushBgMuted);
            } else {
                if (mousehover) painter->fillRect(fillRect, m_brushBgHover);
                else            painter->fillRect(fillRect, m_brushBg);
            }
        }
    }

    if (m_clip->is_muted()) {
        m_waveBrush = m_brushFgMuted;
    } else {
        if (mousehover) m_waveBrush = m_brushFgHover;
        else            m_waveBrush = m_brushFg;
    }

    int channels = m_clip->get_channel_count();

    if (channels > 0) {
        if (m_waitingForPeaks) {
            PMESG("Waiting for peaks!");
            // Hmm, do we paint here something?
            // Progress info, I think so....
            painter->setPen(Qt::black);
            QRect r(10, 0, 150, m_height);
            painter->setFont( themer()->get_font("AudioClip:fontscale:title") );
            QString si;
            si.setNum((int)m_progress);
            if (m_progress == 100) m_progress = 0;
            QString buildProcess = "Building Peaks: " + si + "%";
            painter->drawText(r, Qt::AlignVCenter, buildProcess);

        } else if (m_clip->recording_state() == AudioClip::NO_RECORDING) {
            //                        PROFILE_START;
            draw_peaks(painter, option->exposedRect.x(), pixelcount);
            //                        PROFILE_END("draw peaks");
        }
    }

    draw_clipinfo_area(painter, xstart);

    // Draw the db lines at 0 and -6 db
    if (m_drawDbGrid) {
        draw_db_lines(painter, xstart, pixelcount);
    }

    // Draw the contour
    // painter->setPen(themer()->get_color("AudioClip:contour"));
    // painter->drawRect(m_boundingRect.adjusted(0, 0, -1.5, -1));

    // Paint a pixmap if the clip is locked
    if (m_clip->is_locked()) {
        int center = (int)(m_clip->get_length() / (2 * m_sv->timeref_scalefactor));
        painter->drawPixmap(center - 8, m_height - 20, find_pixmap(":/lock"));
    }

    painter->restore();
}

void AudioClipView::draw_peaks(QPainter* p, qreal xstart, int pixelcount)
{
    PENTER4;

    Peak* peak = m_clip->get_peak();

    // clip away the outline which are painted again vertically
    // in Qt 4.6.x, it doesn't happen in Qt 4.5.x
    // FIXME: find out why?
    if (xstart > 0) {
        xstart -= 1;
        pixelcount += 2;
    }

    if (!peak) {
        //                PERROR("No Peak object available for clip %s", QS_C(m_clip->get_name()));
        return;
    }

    bool microView = m_sheet->get_hzoom() < 64 ? true : false;
    TTimeRef clipstartoffset = m_clip->get_source_start_location();
    uint channels = m_clip->get_channel_count();
    int peakdatacount = microView ? pixelcount : pixelcount * 2;
    // FIXME: make it so it supports any channel count
    float* pixeldata[6];
    float curveDefaultValue = 1.0;
    int mixCurveData = 0;
    int mixAudioClipCurveData = 0;
    int mixTrackAutomationData = 0;
    CurveView* trackAutomationView = m_tv->get_gain_curve_view();
    mixAudioClipCurveData |= m_gainCurveView->has_nodes();
    mixTrackAutomationData |= trackAutomationView->has_nodes();

    double offset = double(clipstartoffset / m_sv->timeref_scalefactor);

    if (!mixAudioClipCurveData && !mixTrackAutomationData) {
        curveDefaultValue = m_gainCurveView->get_default_value();
        curveDefaultValue *= trackAutomationView->get_default_value();
    }

    QVarLengthArray<float> curveMixdown(peakdatacount);

    if (mixAudioClipCurveData) {
        mixAudioClipCurveData |= m_gainCurveView->get_vector(xstart + offset, peakdatacount, curveMixdown.data());
        mixCurveData |= mixAudioClipCurveData;
    }

    if (mixTrackAutomationData) {
        if (mixAudioClipCurveData) {
            QVarLengthArray<float> trackmixdown(peakdatacount);
            int trackCurveMix = trackAutomationView->get_vector(xstart + pos().x(), peakdatacount, trackmixdown.data());
            if (trackCurveMix) {
                for (int j=0; j<peakdatacount; ++j) {
                    curveMixdown[j] *= trackmixdown[j];
                }
                mixCurveData |= trackCurveMix;
            }
        } else {
            mixTrackAutomationData |= trackAutomationView->get_vector(xstart + pos().x(), peakdatacount, curveMixdown.data());
            mixCurveData |= mixTrackAutomationData;
        }
    }

    for (int i = 0; i < m_FadeCurveViews.size(); ++i) {
        FadeCurveView* view = m_FadeCurveViews.at(i);
        QVarLengthArray<float> fademixdown(peakdatacount);
        int fademix = 0;

        if (mixCurveData) {
            fademix = view->get_vector(xstart, peakdatacount, fademixdown.data());
        } else {
            fademix = view->get_vector(xstart, peakdatacount, curveMixdown.data());
        }

        if (mixCurveData && fademix) {
            for (int j=0; j<peakdatacount; ++j) {
                curveMixdown[j] *= fademixdown[j];
            }
        }

        mixCurveData |= fademix;
    }

    // Load peak data, mix curvedata and start painting it
    // if no peakdata is returned for a certain Peak object, schedule it for loading.
    for (uint chan=0; chan < channels; ++chan) {

        int availpeaks = peak->calculate_peaks(
                    chan,
                    &pixeldata[chan],
            TTimeRef(xstart * m_sv->timeref_scalefactor) + clipstartoffset,
                    peakdatacount,
                    m_sheet->get_hzoom());


        if (peakdatacount != availpeaks) {
            // 			PWARN("peakdatacount != availpeaks (%d, %d)", peakdatacount, availpeaks);
        }

        if (availpeaks == Peak::NO_PEAK_FILE) {
            connect(peak, SIGNAL(progress(int)), this, SLOT(update_progress_info(int)));
            connect(peak, SIGNAL(finished()), this, SLOT (peak_creation_finished()));
            m_waitingForPeaks = true;
            peak->start_peak_loading();
            return;
        }

        if (availpeaks == Peak::PERMANENT_FAILURE || availpeaks == Peak::NO_PEAKDATA_FOUND) {
            return;
        }

        if (m_mergedView && channels == 2 && chan == 0) continue;


        // 		pixelcount = std::min(pixelcount, availpeaks);

        // ClassicView uses both positive and negative values,
        // rectified view: pick the highest value of both
        // Merged view: calculate highest value for all channels,
        // and store it in the first channels pixeldata.
        if (!microView) {
            // if Rectified View, calculate max of the minimum and maximum value.
            if (!m_classicView) {
                for (int i=0, j=0; i < (pixelcount*2); i+=2, ++j) {
                    pixeldata[chan][j] = - std::fabs(f_max(pixeldata[chan][i], - pixeldata[chan][i+1]));
                }
            }

            if (m_mergedView && channels == 2) {
                for (int i = 0; i < (pixelcount*2); ++i) {
                    pixeldata[0][i] = f_max(pixeldata[chan - 1][i], pixeldata[chan][i]);
                }
            }
        }

        if (mixCurveData) {
            int curvemixdownpos = 0;
            if (m_classicView) {
                for (int i = 0; i < (pixelcount*2); ++i) {
                    pixeldata[chan][i++] *= curveMixdown[curvemixdownpos];
                    pixeldata[chan][i] *= curveMixdown[curvemixdownpos];
                    curvemixdownpos++;
                }
            } else {
                for (int i = 0; i < pixelcount; i++) {
                    pixeldata[chan][i] *= curveMixdown[curvemixdownpos];
                    curvemixdownpos++;
                }
            }
        }

        p->save();

        // calculate the height of the area available for peak drawing
        // and if the infoarea is displayed, translate the painter
        // drawing by dy = m_infoAreaHeight
        int height = m_height / channels;


        float scaleFactor = ( (float) height * 0.90 / 2) * m_clip->get_gain() * curveDefaultValue;
        float ytrans;


        // Draw channel seperator horizontal lines, if needed.
        if (channels >= 2 && ! m_mergedView && m_classicView && chan >=1 ) {
            p->save();

            if (m_clip->is_selected()) {
                p->setPen(themer()->get_color("AudioClip:channelseperator:selected"));
            } else {
                p->setPen(themer()->get_color("AudioClip:channelseperator"));
            }

            ytrans = height * chan;
            p->translate(xstart, ytrans);
            p->drawLine(0, 0, pixelcount, 0);
            p->restore();
        }


        // Microview, paint waveform as polyline
        if (microView) {

            m_polygon.clear();
            m_polygon.reserve(pixelcount);

            int bufferPos = 0;

            if (m_mergedView) {
                ytrans = (height / 2) * channels;
                scaleFactor *= channels;
            } else {
                ytrans = (height / 2) + (chan * height);
            }

            p->translate(xstart, ytrans);

            if (m_clip->is_selected()) {
                p->setPen(themer()->get_color("AudioClip:channelseperator:selected"));
            } else {
                p->setPen(themer()->get_color("AudioClip:channelseperator"));
            }

            p->drawLine(0, 0, pixelcount, 0);

            for (int x = 0; x < pixelcount; x++) {
                m_polygon.append( QPointF(x, -scaleFactor * pixeldata[chan][bufferPos++]) );
            }

            if (themer()->get_property("AudioClip:wavemicroview:antialiased", 0).toInt()) {
                p->setRenderHints(QPainter::Antialiasing);
            }

            p->setPen(themer()->get_color("AudioClip:wavemicroview"));
            p->drawPolyline(m_polygon);

            // Macroview, paint waveform with painter
        } else {
            if (m_fillwave) {
                p->setBrush(m_waveBrush);
            }

            if (m_paintWithOutline) {
                if (m_clip->is_muted()) {
                    p->setPen(themer()->get_color("AudioClip:wavemacroview:outline:muted"));
                } else {
                    p->setPen(themer()->get_color("AudioClip:wavemacroview:outline"));
                }
            } else {
                p->setPen(Qt::NoPen);
            }

            scaleFactor = ( (float) height * 0.90 / (Peak::MAX_DB_VALUE * 2)) * m_clip->get_gain() * curveDefaultValue;

            if (m_mergedView) {
                if (m_classicView) {
                    ytrans = (height / 2) * channels;
                    scaleFactor *= channels;
                } else {
                    ytrans = height * channels;
                    scaleFactor *= channels;
                }
            }

            int bufferpos = 0;

            if (m_classicView) {

                if (m_mergedView) {
                    ytrans = (height / 2) * channels;
                } else {
                    ytrans = (height / 2) + (chan * height);
                }

                p->translate(xstart, ytrans);

                m_polygon.clear();
                m_polygon.reserve(pixelcount*2);

                for (int x = 0; x < pixelcount; x++) {
                    m_polygon.append( QPointF(x, -scaleFactor * pixeldata[chan][bufferpos]) );
                    bufferpos+=2;
                }

                bufferpos -= 1;

                for (int x = pixelcount - 1; x >= 0; x--) {
                    m_polygon.append( QPointF(x, scaleFactor * pixeldata[chan][bufferpos]) );
                    bufferpos-=2;
                }

                //                                PROFILE_START;
                p->drawPolygon(m_polygon);
                //                                PROFILE_END("draw wave");

                // Draw 'the' -INF line
                p->setPen(minINFLineColor);
                p->drawLine(0, 0, pixelcount, 0);

            } else {
                scaleFactor =  (float) height * 0.95 * m_clip->get_gain() / Peak::MAX_DB_VALUE * curveDefaultValue;
                ytrans = height + (chan * height);

                if (m_mergedView) {
                    ytrans = height * channels;
                    scaleFactor *= channels;
                }

                p->translate(xstart, ytrans);

                m_polygon.clear();
                m_polygon.reserve(pixelcount + 2);

                for (int x=0; x<pixelcount; x++) {
                    m_polygon.append( QPointF(x, scaleFactor * pixeldata[chan][bufferpos++]) );
                }

                m_polygon.append(QPointF(pixelcount, 0));
                m_polygon.append(QPointF(0,0));

                p->drawPolygon(m_polygon);

            }
        }

        p->restore();
    }
}

void AudioClipView::draw_clipinfo_area(QPainter* p, double xstart)
{
    if (xstart > m_clipInfo.width()) {
        return;
    }

    int margin = 3;
    p->drawPixmap(margin, /*m_height - m_clipInfo.height() -*/ margin, m_clipInfo);
}


void AudioClipView::draw_db_lines(QPainter* p, qreal xstart, int pixelcount)
{
    p->save();

    int channels = m_clip->get_channel_count();
    bool microView = m_sheet->get_hzoom() < 64 ? true : false;
    int linestartpos = xstart;
    if (xstart < m_lineOffset) linestartpos = m_lineOffset;

    if ((m_mergedView) || (channels == 0)) {
        channels = 1;
    }

    // calculate the height of one channel
    int height = m_height / channels;

    p->setPen(themer()->get_color("AudioClip:db-grid"));
    p->setFont( themer()->get_font("AudioClip:fontscale:dblines") );

    if (m_classicView || microView) { // classicView = non-rectified

        // translate the painter to set the first channel center line to 0
        p->translate(0, height / 2);

        // determine the distance of the db line from the center line
        int zeroDb = 0.9 * height / 2;
        int msixDb = 0.9 * height / 4;

        // draw the lines above and below the center line, then translate
        // the painter to the next channel
        for (int i = 0; i < channels; ++i) {
            p->drawLine(linestartpos, zeroDb, xstart+pixelcount, zeroDb);
            p->drawLine(linestartpos, -zeroDb, xstart+pixelcount, -zeroDb);
            p->drawLine(linestartpos, msixDb, xstart+pixelcount, msixDb);
            p->drawLine(linestartpos, -msixDb + 1, xstart+pixelcount, -msixDb + 1);

            if (xstart < m_lineOffset) {
                p->drawText(0.0, zeroDb - 1 + m_lineVOffset, "  0 dB");
                p->drawText(0.0, -zeroDb + m_lineVOffset, "  0 dB");
                p->drawText(0.0, msixDb + m_lineVOffset, " -6 dB");
                p->drawText(0.0, -msixDb + m_lineVOffset, " -6 dB");
            }


//            p->translate(0, height);
        }
    } else {  // rectified

        // translate the painter to set the first channel base line to 0
        p->translate(0, height);

        // determine the distance of the db line from the center line
        int zeroDb = 0.95 * height;
        int msixDb = 0.95 * height / 2;

        // draw the lines above the center line, then translate
        // the painter to the next channel
        for (int i = 0; i < channels; ++i) {
            p->drawLine(linestartpos, -zeroDb, xstart+pixelcount, -zeroDb);
            p->drawLine(linestartpos, -msixDb + 1, xstart+pixelcount, -msixDb + 1);

            if (xstart < m_lineOffset) {
                p->drawText(0.0, -zeroDb + m_lineVOffset, "  0 dB");
                p->drawText(0.0, -msixDb + m_lineVOffset, " -6 dB");
            }

//            p->translate(0, height);
        }
    }

    p->restore();
}

void AudioClipView::create_brushes()
{
    /** TODO: The following part is identical to calculations in draw_db_lines(). Move to a central place. **/
    bool microView = m_sheet->get_hzoom() < 64 ? true : false;
    int channels = m_clip->get_channel_count();

    if ((m_mergedView) || (channels == 0)) {
        channels = 1;
    }

    // calculate the height of one channel
    int height = m_height / channels;

    if (m_classicView || microView)
    {
        height *= 0.45;
    } else {
        height *= 0.95;
    }
    /** end of TODO **/

    // create brushes for background states
    m_brushBgRecording = themer()->get_brush("AudioClip:background:recording", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgMuted = themer()->get_brush("AudioClip:background:muted", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgMutedHover = themer()->get_brush("AudioClip:background:muted:mousehover", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgSelected  = themer()->get_brush("AudioClip:background:selected", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgSelectedHover = themer()->get_brush("AudioClip:background:selected:mousehover", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBg = themer()->get_brush("AudioClip:background", QPoint(0, 0), QPoint(0, -m_height));
    m_brushBgHover = themer()->get_brush("AudioClip:background:mousehover", QPoint(0, 0), QPoint(0, -m_height));

    // brushes for the wave form
    m_brushFg = themer()->get_brush("AudioClip:wavemacroview:brush", QPoint(0, 0), QPoint(0, -height));
    m_brushFgHover = themer()->get_brush("AudioClip:wavemacroview:brush:hover", QPoint(0, 0), QPoint(0, -height));
    m_brushFgMuted = themer()->get_brush("AudioClip:wavemacroview:brush:muted", QPoint(0, 0), QPoint(0, -height));
    m_brushFgEdit = themer()->get_brush("AudioClip:wavemacroview:brush:curvemode", QPoint(0, 0), QPoint(0, -height));
    m_brushFgEditHover = themer()->get_brush("AudioClip:wavemacroview:brush:curvemode:hover", QPoint(0, 0), QPoint(0, -height));
}

void AudioClipView::create_clipinfo_string()
{
    PENTER;
    QFont font = themer()->get_font("AudioClip:fontscale:title");
    QFontMetrics fm(font);

    QString clipinfoString = fm.elidedText(m_clip->get_name(), Qt::ElideRight, 200);

    int clipInfoWidth = fm.boundingRect(clipinfoString).width();

    m_clipInfo = QPixmap(clipInfoWidth, m_infoAreaHeight);
    m_clipInfo.fill(Qt::transparent);
    QColor textColor = themer()->get_color("AudioClip:text");

    QPainter painter(&m_clipInfo);
    painter.setFont(font);
    painter.setPen(textColor);
    painter.drawText(m_clipInfo.rect(), clipinfoString);
}

void AudioClipView::update_progress_info( int progress )
{
    m_progress = progress;
    update(10, 0, 150, m_height);
}

void AudioClipView::peak_creation_finished()
{
    m_waitingForPeaks = false;
    update();
}

void AudioClipView::add_new_fade_curve_view( FadeCurve * fade )
{
    PENTER;
    FadeCurveView* view = new FadeCurveView(m_sv, this, fade);
    m_FadeCurveViews.append(view);
    connect(view, SIGNAL(fadeModified()), m_sv, SLOT(stop_follow_play_head()));
}

void AudioClipView::remove_fade_curve_view( FadeCurve * fade )
{
    for (int i = 0; i < m_FadeCurveViews.size(); ++i) {
        FadeCurveView* view = m_FadeCurveViews.at(i);
        if (view->get_fade() == fade) {
            m_FadeCurveViews.takeAt(i);
            scene()->removeItem(view);
            delete view;
            break;
        }
    }
}

void AudioClipView::calculate_bounding_rect()
{
    PENTER4;
    prepareGeometryChange();

    m_height = m_parentViewItem->get_height();
    m_boundingRect = QRectF(0, 0, (double(m_clip->get_length().universal_frame()) / m_sv->timeref_scalefactor), m_height);

    uint channelCount = m_clip->get_channel_count();

    // A silent clip readsource on purpose has 0 channels
    // catch and deal with this to avoid a division by 0 below
    if  (channelCount == 0) {
        channelCount = 1;
    }

    if ((m_height / int(channelCount)) < 30) {
        m_classicView = false;
    } else {
        m_classicView = ! config().get_property("Themer", "paintaudiorectified", false).toBool();
    }

    update_start_pos();
    ViewItem::calculate_bounding_rect();
}


void AudioClipView::repaint( )
{
    update(m_boundingRect);
}

void AudioClipView::update_start_pos()
{
    // 	printf("AudioClipView::update_start_pos()\n");
    setPos((double(m_clip->get_location()->get_start().universal_frame()) / m_sv->timeref_scalefactor), 0);
}

TCommand * AudioClipView::fade_range()
{
    Q_ASSERT(m_sheet);
    qreal x = cpointer().on_first_input_event_scene_x() - scenePos().x();

    if (x < (m_boundingRect.width() / 2)) {
        return clip_fade_in();
    } 
    return clip_fade_out();
}

TCommand * AudioClipView::clip_fade_in( )
{
    if (! m_clip->get_fade_in()) {
        // This implicitely creates the fadecurve
        m_clip->set_fade_in(1);
    }
    return new FadeRange(m_clip, m_clip->get_fade_in(), m_sv->timeref_scalefactor);
}

TCommand * AudioClipView::clip_fade_out( )
{
    if (! m_clip->get_fade_out()) {
        m_clip->set_fade_out(1);
    }
    return new FadeRange(m_clip, m_clip->get_fade_out(), m_sv->timeref_scalefactor);
}

TCommand * AudioClipView::reset_fade()
{
    Q_ASSERT(m_sheet);
    qreal x = cpointer().on_first_input_event_scene_x() - scenePos().x();

    if (x < (m_boundingRect.width() / 2)) {
        return m_clip->reset_fade_in();
    } 
    return m_clip->reset_fade_out();
}

void AudioClipView::position_changed()
{
    // Update the curveview start offset, only needed for left edge dragging
    // but who cares :)
    // the calculate_bounding_rect() will update AudioClipViews children, so
    // the CurveView and it's nodes get updated as well, no need to set
    // the start offset for those manually!
    m_gainCurveView->set_start_offset(m_clip->get_source_start_location());
    calculate_bounding_rect();
}

void AudioClipView::load_theme_data()
{
    m_drawbackground = themer()->get_property("AudioClip:drawbackground", 1).toInt();
    m_infoAreaHeight = themer()->get_property("AudioClip:infoareaheight", 16).toInt();
    m_mimimumheightforinfoarea = themer()->get_property("AudioClip:mimimumheightforinfoarea", 45).toInt();
    m_classicView = ! config().get_property("Themer", "paintaudiorectified", false).toBool();
    m_mergedView = config().get_property("Themer", "paintstereoaudioasmono", false).toBool();
    m_fillwave = themer()->get_property("AudioClip:fillwave", 1).toInt();
    minINFLineColor = themer()->get_color("AudioClip:channelseperator");
    m_paintWithOutline = config().get_property("Themer", "paintwavewithoutline", true).toBool();
    m_drawDbGrid = config().get_property("Themer", "drawdbgrid", false).toBool();
    AudioClipView::calculate_bounding_rect();

    QFont dblfont = themer()->get_font("AudioClip:fontscale:dblines");
    QFontMetrics fm(dblfont);
    m_lineOffset = fm.horizontalAdvance(" -6 dB ");
    m_lineVOffset = fm.ascent()/2;

    create_brushes();
    create_clipinfo_string();
}


void AudioClipView::active_context_changed()
{
    if (ied().is_holding()) {
        // TODO: find out if we still need to bail out
        // when holding is active, say for moving a clip with [ D ]
        // or something else?
        //                return;
    }

    if (m_clip->has_active_context()) {
        m_tv->to_front(this);
    }

    update(m_boundingRect);
}


TCommand * AudioClipView::select_fade_in_shape( )
{
    TMainWindow::instance()->select_fade_in_shape();

    return nullptr;
}

TCommand * AudioClipView::select_fade_out_shape( )
{
    TMainWindow::instance()->select_fade_out_shape();

    return nullptr;
}

void AudioClipView::start_recording()
{
    m_oldRecordingPos = TTimeRef();
    connect(&m_recordingTimer, SIGNAL(timeout()), this, SLOT(update_recording()));
    m_recordingTimer.start(750);
}

void AudioClipView::finish_recording()
{
    m_recordingTimer.stop();
    prepareGeometryChange();
    m_boundingRect = QRectF(0, 0, (m_clip->get_length() / m_sv->timeref_scalefactor), m_height);
    m_gainCurveView->calculate_bounding_rect();
    update();
}

void AudioClipView::update_recording()
{
    if (m_clip->recording_state() != AudioClip::RECORDING) {
        return;
    }

    TTimeRef newPos = m_clip->get_length();
    m_boundingRect = QRectF(0, 0, (newPos / m_sv->timeref_scalefactor), m_height);

    int updatewidth = int((newPos - m_oldRecordingPos) / m_sv->timeref_scalefactor);
    QRect updaterect = QRect(int(m_oldRecordingPos / m_sv->timeref_scalefactor) - 1, 0, updatewidth + 1, m_height);
    update(updaterect);
    m_oldRecordingPos = newPos;
}

TCommand * AudioClipView::set_audio_file()
{
    if (m_clip->is_readsource_invalid()) {
        ReadSource* rs = m_clip->get_readsource();
        if ( ! rs ) {
            return ied().failure();
        }

        QString filename = QFileDialog::getOpenFileName(TMainWindow::instance(),
                                                        tr("Reset Audio File for Clip: %1").arg(m_clip->get_name()),
                                                        rs->get_filename(),
                                                        tr("All files (*);;Audio files (*.wav *.flac)"));

        if (filename.isEmpty()) {
            info().information(tr("No file selected!"));
            return ied().failure();
        }

        if (rs->set_file(filename) < 0) {
            return ied().failure();
        }

        resources_manager()->set_source_for_clip(m_clip, rs);


        // FIXME This is a hack. When a ReadSource didn't have a valid file it wasn't added
        // to DiskIO in AudioClip::set_sheet(). So when resetting the audiofile this solves it,
        // but it's not the proper place to do so!!
        m_clip->set_sheet(m_sheet);

        info().information(tr("Succesfully set AudioClip file to %1").arg(filename));

        return ied().succes();
    }

    return ied().did_not_implement();
}

TCommand * AudioClipView::edit_properties()
{
    auto editdialog = new AudioClipEditDialog(m_clip, TMainWindow::instance());

    editdialog->show();

    return nullptr;
}

void AudioClipView::clip_state_changed()
{
    update();
}

