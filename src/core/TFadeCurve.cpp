/*
Copyright (C) 2006 Remon Sijrier, Nicola Doebelin

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

$Id: TFadeCurve.cpp,v 1.36 2008/11/07 10:43:08 r_sijrier Exp $
*/

#include "TFadeCurve.h"

#include <QFile>
#include <cmath>
#include "TCurveNode.h"
#include "Fade.h"
#include "TCommand.h"
#include "CommandGroup.h"
#include "TAddRemoveCommand.h"
#include "TAudioDevice.h"
#include "AudioBus.h"
#include "TLocation.h"
#include "Debugger.h"
#include "TSession.h"


QStringList TFadeCurve::defaultShapes = QStringList() << "Fastest" << "Fast" << "Linear"  << "Slow" << "Slowest";
TFadeCurve::TFadeCurve(TContextItem *parent, FadeType fadeType )
    : TCurve(parent)
    , m_parentLocation(nullptr)
    , m_type(fadeType)
{
    m_controlPoints.append(QPointF(0.0, 0.0));
    m_controlPoints.append(QPointF(0.25, 0.25));
    m_controlPoints.append(QPointF(0.75, 0.75));
    m_controlPoints.append(QPointF(1.0, 1.0));

    m_bendFactor = 0.0;
    m_strenghtFactor = 0.5;
    m_mode = 2;
    m_raster = 0;
    m_bypass = false;

    next = nullptr;

    connect(this, &TFadeCurve::stateChanged, this, &TFadeCurve::solve_node_positions);
    connect(this, &TFadeCurve::bendValueChanged, this, &TFadeCurve::stateChanged);
    connect(this, &TFadeCurve::strengthValueChanged, this, &TFadeCurve::stateChanged);
    connect(this, &TFadeCurve::modeChanged, this, &TFadeCurve::stateChanged);

    init();
}

TFadeCurve::~ TFadeCurve( )
{
    PENTERDES;
}

void TFadeCurve::init()
{
    QObject::tr("TFadeCurve");

    // Populate the curve with 12 CurveNodes
    float f = 0.0;
    int nodecount = 11;
    for (int i = 0; i <= nodecount; ++i) {
        QPointF p = get_curve_point(f);

        TCurveNode* node = new TCurveNode(this, p.x(), p.y());
        TAddRemoveCommand* cmd = (TAddRemoveCommand*) add_node(node, false);
        cmd->set_instantanious(true);
        TCommand::process_command(cmd);

        // 		printf("adding node with x=%f, y=%f\n", p.x(), p.y());

        f += 1.0 / nodecount;
    }

    set_range(1.0);
}

QDomNode TFadeCurve::get_state( QDomDocument doc )
{
    QDomElement node = doc.createElement(fade_type_to_string());
    node.setAttribute("bendfactor", m_bendFactor);
    node.setAttribute("strengthfactor", m_strenghtFactor);
    node.setAttribute("bypassed", m_bypass);
    node.setAttribute("range", get_range());
    node.setAttribute("mode", m_mode);
    node.setAttribute("raster", m_raster);

    QStringList controlPointsList;

    for (int i=0; i< m_controlPoints.size(); ++i) {
        QPointF point = m_controlPoints.at(i);

        controlPointsList << QString::number(point.x()).append(",").append(QString::number(point.y()));
    }

    node.setAttribute("controlpoints",  controlPointsList.join(";"));

    return node;
}

int TFadeCurve::set_state( const QDomNode & node )
{
    QDomElement e = node.toElement();
    m_bendFactor = e.attribute( "bendfactor", "0.0" ).toDouble();
    m_strenghtFactor = e.attribute( "strengthfactor", "0.5" ).toDouble();
    m_bypass = e.attribute( "bypassed", "0" ).toInt();
    m_mode = e.attribute( "mode", "2" ).toInt();
    m_raster = e.attribute( "raster", "0" ).toInt();

    QStringList controlPointsList = e.attribute( "controlpoints", "0.0,0.0;0.25,0.25;0.75,0.75;1.0,1.0" ).split(";");


    for (int i=0; i<controlPointsList.size(); ++i) {
        QStringList xyList = controlPointsList.at(i).split(",");
        float x = xyList.at(0).toFloat();
        float y = xyList.at(1).toFloat();
        m_controlPoints[i] = QPointF(x,y);
    }


    QString rangestring = e.attribute("range", "1");
    double range;
    if (rangestring == "nan" || rangestring == "inf") {
        printf("FadeCurve::set_state: stored range was not a number!\n");
        range = 1;
    } else {
        range = rangestring.toDouble();
    }

    range = (range < 1.0) ? 1 : range;

    set_range(range);

    solve_node_positions();

    emit stateChanged();

    return 1;
}


void TFadeCurve::process(const TAudioBuffer &curveBuffer, AudioBus *bus, const TTimeRef& startLocation, const TTimeRef& endLocation, nframes_t nframes)
{
    Q_ASSERT(bus->get_channel_count() == 2);
    Q_ASSERT(m_parentLocation);
    if (is_bypassed()) {
        return;
    }
    uint outputRate = audiodevice().get_sample_rate();
    uint framesToProcess = nframes;
    uint channelCount = bus->get_channel_count();

    TTimeRef fadeStartLocation, fadeEndLocation, fadeLocation;
    TTimeRef fadeRange = TTimeRef(get_range());

    TTimeRef upperRange = startLocation + TTimeRef(nframes, outputRate);


    if (m_type == FadeIn) {
        fadeStartLocation = m_parentLocation->get_start();
    } else {
        fadeStartLocation = m_parentLocation->get_end() - fadeRange;
    }    

    fadeEndLocation = fadeStartLocation + fadeRange;

    if ((startLocation >= fadeEndLocation || (endLocation <= fadeStartLocation))) {
        return;
    }

    nframes_t offset = 0;

    if (startLocation < fadeStartLocation) {
        offset = TTimeRef::to_frame(fadeStartLocation - startLocation, outputRate);
        Q_ASSERT(offset <= nframes);
        fadeLocation = TTimeRef();
        framesToProcess = framesToProcess - offset;
    } else {
        fadeLocation = (startLocation - fadeStartLocation);
    }

    if (fadeEndLocation < upperRange) {
        framesToProcess -= TTimeRef::to_frame(upperRange - fadeEndLocation, outputRate);
    }

    if (framesToProcess == 0) {
        // After recording a new clip, and playing it back, an assert in set_data_start_offset was
        // triggering when framesToProcess was 0, which happened when playing the very end of the clip.
        return;
    }
    upperRange = fadeLocation + TTimeRef(framesToProcess, outputRate);

    get_vector(fadeLocation.universal_frame(), upperRange.universal_frame(), curveBuffer, framesToProcess);

    for (uint chan=0; chan<channelCount; ++chan) {
        TAudioBuffer &buf = bus->get_buffer(chan); //, framesToProcess, offset);
        buf.set_data_start_offset(offset);
        for (nframes_t frame = 0; frame < framesToProcess; ++frame) {
            buf[frame] *= curveBuffer.at(frame);
        }
        buf.set_data_start_offset(0);
    }
}


void TFadeCurve::set_shape(const QString& shapeName)
{
    QDomDocument doc("FadeShapes");

    if (defaultShapes.contains(shapeName)) {
        QFile file(":/fadeshapes");

        if (!file.open(QIODevice::ReadOnly)) {
            printf("Could not open fadeshapes file!!\n");
            return;
        }
        if (!doc.setContent(&file)) {
            file.close();
            printf("Could not set QDomDocument content!\n");
            return;
        }
        file.close();
    } else {
        // Load from custom saved fades
    }


    QDomElement root = doc.documentElement();
    QDomNode node = root.firstChildElement(fade_type_to_string());

    QDomElement fadeElement = node.firstChildElement(shapeName);

    if (fadeElement.isNull()) {
        printf("%s does not exist?????\n", shapeName.toLatin1().data());
        return;
    }

    CommandGroup* group = new CommandGroup(this, tr("Fade Preset"));

    group->add_command(new FadeBend(this, fadeElement.attribute( "bendfactor", "0.0" ).toDouble()));
    group->add_command(new FadeStrength(this, fadeElement.attribute( "strengthfactor", "0.5" ).toDouble()));

    TCommand::process_command(group);

    emit stateChanged();
}

void TFadeCurve::solve_node_positions( )
{
    // calculate control points values
    if (m_mode == 0) { // bended
        if (m_type == FadeIn) {
            m_controlPoints[1] = QPointF(m_strenghtFactor * (1.0 - m_bendFactor), m_strenghtFactor * m_bendFactor);
            m_controlPoints[2] = QPointF(1.0 - (m_strenghtFactor * m_bendFactor), 1.0 - (m_strenghtFactor * (1.0 - m_bendFactor)));
        }
        if (m_type == FadeOut) {
            m_controlPoints[1] = QPointF(m_strenghtFactor * m_bendFactor, m_strenghtFactor * (1.0 - m_bendFactor));
            m_controlPoints[2] = QPointF(1.0 - (m_strenghtFactor * (1.0 - m_bendFactor)), 1.0 - (m_strenghtFactor * m_bendFactor));
        }
    }
    if (m_mode == 1) { // s-shape
        m_controlPoints[1] = QPointF(m_strenghtFactor * (1.0 - m_bendFactor), m_strenghtFactor * m_bendFactor);
        m_controlPoints[2] = QPointF(1.0 - (m_strenghtFactor * (1.0 - m_bendFactor)), 1.0 - (m_strenghtFactor * m_bendFactor));
    }
    if (m_mode == 2) { // long
        if (m_type == FadeIn) {
            m_controlPoints[1] = QPointF(m_strenghtFactor * (1.0 - m_bendFactor), m_strenghtFactor * m_bendFactor);
            m_controlPoints[2] = QPointF(1.0, 1.0);
        }
        if (m_type == FadeOut) {
            m_controlPoints[1] = QPointF(0.0, 0.0);
            m_controlPoints[2] = QPointF(1.0 - (m_strenghtFactor * (1.0 - m_bendFactor)), 1.0 - (m_strenghtFactor * m_bendFactor));
        }
    }


    // calculate curve nodes values
    float f = 0.0;
    TRealTimeLinkedList<TCurveNode*> list = TCurve::get_nodes();
    int listsize = list.size();
    if (listsize > 0) {
        TCurveNode* node = list.first()->next;

        while (node) {
            f += 1.0 / (listsize - 1);
            QPointF p = get_curve_point(f);
            node->set_relative_when_and_value(p.x(), p.y());
            node = node->next;
        }
    }

    set_changed();
}

QPointF TFadeCurve::get_curve_point( float f)
{
    float x = m_controlPoints.at(0).x() * pow((1.0 - f), 3.0)
    + 3 * m_controlPoints.at(1).x() * f * pow((1.0 - f), 2.0)
        + 3 * m_controlPoints.at(2).x() * pow(f, 2.0) * (1.0 - f)
        + m_controlPoints.at(3).x() * pow(f, 3.0);

    float y = m_controlPoints.at(0).y() * pow((1.0 - f), 3.0)
              + 3 * m_controlPoints.at(1).y() * f * pow((1.0 - f), 2.0)
              + 3 * m_controlPoints.at(2).y() * pow(f, 2.0) * (1.0 - f)
              + m_controlPoints.at(3).y() * pow(f, 3.0);

    if (m_type == FadeOut) {
        y = 1.0 - y;
    }

    return QPointF(x, y);
}

void TFadeCurve::set_range(double range)
{
    if (range <= 1.0) {
        range = 1.0;
    }

    TCurve::set_range(range);
    emit rangeChanged();
}

void TFadeCurve::set_bend_factor( float factor )
{
    if (factor > 1.0)
        factor = 1.0;
    if (factor < 0.0)
        factor = 0.0;

    m_bendFactor = factor;

    emit bendValueChanged();
}

void TFadeCurve::set_strength_factor( float factor )
{
    if (factor > 1.0)
        factor = 1.0;
    if (factor < 0.0)
        factor = 0.0;

    m_strenghtFactor = factor;

    emit strengthValueChanged();
}

QList< QPointF > TFadeCurve::get_control_points( ) const
{
    return m_controlPoints;
}

TCommand* TFadeCurve::set_mode( )
{
    if (m_mode < 2) {
        return new FadeMode(this, m_mode, m_mode+1);
    } else {
        return new FadeMode(this, m_mode, 0);
    }
}

void TFadeCurve::set_mode(int m)
{
    m_mode = m;

    emit modeChanged();
}

QString TFadeCurve::fade_type_to_string() const
{
    if(m_type == FadeIn) {
        return QString("FadeIn");
    }
    if (m_type == FadeOut) {
        return QString("FadeOut");
    }
    return QString("Unknown Fade Type to String Conversion");
}

TCommand* TFadeCurve::toggle_bypass( )
{
    m_bypass = !m_bypass;

    emit stateChanged();
    return 0;
}

TCommand* TFadeCurve::toggle_raster( )
{
    m_raster = ! m_raster;

    emit rasterChanged();
    return 0;
}

