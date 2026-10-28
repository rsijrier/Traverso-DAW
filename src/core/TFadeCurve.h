/*
Copyright (C) 2006 - 2024 Remon Sijrier

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

$Id: FadeCurve.h,v 1.19 2008/01/21 16:22:14 r_sijrier Exp $
*/

#pragma once

#include "TCommand.h"
#include "TContextItem.h"
#include "TCurveSnapshot.h"
#include "TFadeShape.h"
#include "TRtValue.h"
#include "TChannelBuffer.h"
#include "TTimeRef.h"

#include <QDomDocument>
#include <QString>
#include <QStringList>
#include <QList>
#include <QPointF>

#include <array>
#include <memory>

class TProcessContext;
class AudioBus;
class TTimeLineEntity;
class TRtTransaction;

// What the audio thread needs of a fade, published as one snapshot
struct TFadeState
{
    std::shared_ptr<const TCurveSnapshot> shape;    // time and value 0..1
    TTimeRef                              length;   // 0: off
    bool                                  bypassed;
};

/**
 * @brief A fade in or fade out of a clip.
 *
 * The shape (mode, bend and strength) is solved once, as a curve with time
 * and value from 0 to 1, see TFadeShape. The length is separate, a fade of
 * length 0 is off but keeps its shape. Shape, length and bypass reach the
 * audio thread together as one TFadeState; changing only the length publishes
 * the same shape with the new length.
 */
class TFadeCurve : public TContextItem
{
    Q_OBJECT

public:
    static QStringList defaultShapes;

    // Defined with the shape, which depends on it
    using FadeType = TFadeShape::FadeType;
    static constexpr FadeType FadeIn = TFadeShape::FadeIn;
    static constexpr FadeType FadeOut = TFadeShape::FadeOut;

    TFadeCurve(TContextItem* parent, FadeType fadeType, TRtTransaction& transaction);
    ~TFadeCurve() override = default;

    QDomNode get_state(QDomDocument doc);
    int set_state( const QDomNode & node );


    // Audio thread
    // context: the cycle (its rate is the rate the device was granted)
    void process(const TProcessContext& context, const TChannelBuffer &curveBuffer, AudioBus* bus, const TTimeRef &startLocation, const TTimeRef &endLocation, nframes_t nframes) T_REALTIME_SAFE;

    // GUI thread

    float get_bend_factor() {return m_bendFactor;}
    float get_strength_factor() {return m_strenghtFactor;}
    int get_mode() const {return m_mode;}
    int get_raster() const {return m_raster;}
    // 0 when the fade is off
    TTimeRef get_length() const {return m_length;}

    void set_shape(const QString &shapeName);
    void set_bend_factor(float factor, TRtTransaction& transaction);
    void set_strength_factor(float factor, TRtTransaction& transaction);
    void set_mode(int m, TRtTransaction& transaction);
    // A length of 0 turns the fade off, a negative one counts as 0
    void set_length(const TTimeRef& length, TRtTransaction& transaction);

    void set_parent_timeline_entity(TTimeLineEntity* entity) {
        m_parentTimeLineEntity = entity;
    }

    FadeType get_fade_type() const {return m_type;}
    QList<QPointF> get_control_points() const;

    bool is_bypassed() const {return m_bypass;}

    QString fade_type_to_string() const;

    // The fade's gain between x0 and x1, in universal frames from the start of
    // the fade, and for drawing, see TCurveSnapshot::get_columns()
    void get_vector(double x0, double x1, const TChannelBuffer &audioBuffer, nframes_t veclen);
    void get_columns(double x0, double columnWidth, int count, TCurveColumn* columns);

private:
    TTimeLineEntity*                      m_parentTimeLineEntity{nullptr};
    float 		                          m_bendFactor{0.0f};
    float 		                          m_strenghtFactor{0.5f};
    bool		                          m_bypass{false};
    int 		                          m_mode{2};
    int                                   m_raster{0};
    TTimeRef                              m_length;
    FadeType	                          m_type;
    std::array<QPointF, 4>                m_controlPoints;
    std::shared_ptr<const TCurveSnapshot> m_shape;
    TRtValue<TFadeState>                  m_state;
    TCurveLookupCache                     m_guiLookupCache;
    TCurveLookupCache                     m_rtLookupCache;

    // Solves the shape for the mode, bend and strength
    void solve_shape(TRtTransaction& transaction);
    std::shared_ptr<const TFadeState> create_state() const;

public slots:
    TCommand* toggle_bypass();
    TCommand* set_mode();
    TCommand* toggle_raster();

signals:
    void stateChanged();
    void modeChanged();
    void bendValueChanged();
    void strengthValueChanged();
    void rasterChanged();
    void lengthChanged();
};

