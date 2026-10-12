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

#ifndef TFADE_CURVE_H
#define TFADE_CURVE_H

#include "TCommand.h"
#include "TCurve.h"

#include <QString>
#include <QStringList>
#include <QList>
#include <QPointF>

class AudioBus;
class TLocation;

class TFadeCurve : public TCurve
{
    Q_OBJECT

public:
    static QStringList defaultShapes;

    enum FadeType {
        FadeIn = 0,
        FadeOut = 1
    };

    TFadeCurve(TContextItem* parent, FadeType fadeType);
    ~TFadeCurve();
	
	
	QDomNode get_state(QDomDocument doc);
	int set_state( const QDomNode & node );

    void process(const TAudioBuffer &curveBuffer, AudioBus* bus, const TTimeRef &startLocation, const TTimeRef &endLocation, nframes_t nframes);
	
	float get_bend_factor() {return m_bendFactor;}
	float get_strength_factor() {return m_strenghtFactor;}
	int get_mode() const {return m_mode;}
	int get_raster() const {return m_raster;}
	
	void set_shape(const QString &shapeName);
	void set_bend_factor(float factor);
	void set_strength_factor(float factor);

    void set_parent_location(TLocation* location) {
        m_parentLocation = location;
    }

    FadeType get_fade_type() const {return m_type;}
    QList<QPointF> get_control_points() const;

    bool is_bypassed() const {return m_bypass;}

    void set_mode(int m);

    QString fade_type_to_string() const;

    bool operator<(const TFadeCurve& /*other*/) {
        return false;
    }

    TFadeCurve* next;

private:
    TLocation*  m_parentLocation;
	float 		m_bendFactor;
	float 		m_strenghtFactor;
	bool		m_bypass;
	int 		m_mode;
    int         m_raster;
	FadeType	m_type;
	QList<QPointF> 	m_controlPoints;
	
	QPointF get_curve_point(float f);
	void init();
	
public slots:
	void solve_node_positions();
    void set_range(double range);

    DISPATCH_RULE_IS_ALWAYS TCommand* toggle_bypass();
    DISPATCH_RULE_IS_ALWAYS TCommand* set_mode();
    DISPATCH_RULE_IS_ALWAYS TCommand* toggle_raster();

signals:
    void modeChanged();
    void bendValueChanged();
    void strengthValueChanged();
    void rasterChanged();
    void rangeChanged();
};

#endif

//eof

