/*
Copyright (C) 2010-2019 Remon Sijrier

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

#include "Gain.h"

#include "ContextItem.h"
#include "ContextPointer.h"
#include "Sheet.h"
#include "TBusTrack.h"
#include "Utils.h"
#include "Mixer.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

/**
 *	\class Gain
    \brief Change (jog) the Gain of an TAudioProcessingNode, or set to a pre-defined value

    \sa TraversoCommands
 */


Gain::Gain(ContextItem* context, const QVariantList& /*args*/)
    : TCommand(context, "")
{
    m_gainObject = context;
    m_newGain = m_origGain = get_gain_from_object(m_gainObject);

    Sheet* sheet = qobject_cast<Sheet*>(context);
    if (sheet) {
        // if context == sheet, then use sheets master out
        // as the gain object as sheet itself doesn't apply any gain.
        m_gainObject = sheet->get_master_out_bus_track();
    }
}

Gain::~Gain()
{
    PENTERDES;
}

int Gain::prepare_actions()
{
    if (qFuzzyCompare(m_origGain, m_newGain)) {
        // Nothing happened!
        return -1;
    }
    return 1;
}

void Gain::apply_new_gain_to_object(float newGain)
{
    m_newGain = newGain;
    QMetaObject::invokeMethod(m_gainObject, "set_gain", Q_ARG(float, m_newGain));
    // the gainobject is able to refuse the new value, so we set our
    // newGain value to the value the gainobject internally decided to go for
    m_newGain = get_gain_from_object(m_gainObject);
}

int Gain::do_action()
{
    PENTER;

    // We already set the new gain value during process_mouse_move()
    // however, do_action() is always called from the TInputEventDispatcher
    // So do not start the animated gain setting since it will start from
    // the m_oldgain value.
    if (qFuzzyCompare(m_newGain, get_gain_from_object(m_gainObject))) {
        return 1;
    }

    // so this will only be reached after an undo/redo sequence
    QMetaObject::invokeMethod(m_gainObject, "set_gain_animated", Q_ARG(float, m_newGain));

    return 1;
}

int Gain::undo_action()
{
    PENTER;

    QMetaObject::invokeMethod(m_gainObject, "set_gain_animated", Q_ARG(float, m_origGain));

    return 1;
}

void Gain::cancel_action()
{
    undo_action();
}

void Gain::increase_gain(  )
{
    audio_sample_t dbFactor = coefficient_to_dB(m_newGain);
    dbFactor += 0.2f;
    apply_new_gain_to_object(dB_to_scale_factor(dbFactor));
}

void Gain::decrease_gain()
{
    audio_sample_t dbFactor = coefficient_to_dB(m_newGain);
    dbFactor -= 0.2f;
    apply_new_gain_to_object(dB_to_scale_factor(dbFactor));
}

void Gain::set_new_gain(float newGain)
{
    m_newGain = newGain;
    do_action();
}

void Gain::set_new_gain_numerical_input(float newGain)
{
    m_newGain = newGain;
}

int Gain::process_mouse_move(qreal diffY)
{
    qreal of = 0;
    audio_sample_t dbFactor = coefficient_to_dB(m_newGain);


    if (dbFactor > -1) {
        of = diffY * 0.05;
    }
    if (dbFactor <= -1) {
        of = diffY * ((1 - double(dB_to_scale_factor(dbFactor))) / 3);
    }

    apply_new_gain_to_object(dB_to_scale_factor(dbFactor + float(of)));

    return 1;
}

float Gain::get_gain_from_object(QObject *object)
{
    float gain = 1.0f;

    if ( ! QMetaObject::invokeMethod(object, "get_gain",
                                     Qt::DirectConnection,
                                     Q_RETURN_ARG(float, gain)) ) {
        PWARN("Gain::get_gain_from_object QMetaObject::invokeMethod failed");
    }

    return gain;
}
