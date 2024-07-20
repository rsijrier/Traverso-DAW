/*
Copyright (C) 2006-2019 Remon Sijrier

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

$Id: Tsar.h,v 1.4 2008/02/11 10:11:52 r_sijrier Exp $
*/

#ifndef TSAR_H
#define TSAR_H

#include <QObject>
#include <QThread>

#include "cameron/readerwritercircularbuffer.h"


struct TsarEvent {
    QObject* 	caller;
    void*		argument;
    int         slotindex;
    int         signalindex;
};

class TsarThread : public QThread
{
    Q_OBJECT;
    void run() {exec();}

public slots:
    void process_tsar_signals();
};


class Tsar : public QObject
{
    Q_OBJECT

public:
    void prepare_event(TsarEvent &event, QObject* caller, void* argument, const char* slotSignature, const char* signalSignature);

    void post_gui_event(const TsarEvent &event);
    void post_rt_event(const TsarEvent& event);

    void process_event(const TsarEvent &event);
    void process_event_slot(const TsarEvent& event);
    void process_event_signal(const TsarEvent &event);

    [[deprecated]] void add_rt_event(QObject *cal, void* arg, const char* signalSignature);
    [[deprecated]] void add_gui_event(QObject* caller, void* arg, const char* slotSignature, const char* signalSignature);

private:
    Tsar();
    ~Tsar();
    Tsar(const Tsar&);

    // allow this function to create one instance
    friend Tsar& tsar();

    // The AudioDevice instance is the _only_ one who
    // is allowed to call process_events_slot() !!
    friend class AudioDevice;
    friend class TsarThread;

    moodycamel::BlockingReaderWriterCircularBuffer<TsarEvent>*   m_blockingGuiThreadEventBuffer;
    moodycamel::BlockingReaderWriterCircularBuffer<TsarEvent>*   m_blockingEmitEventSignalsInGuiThreadQueue;

    int             m_eventCounter;
    int             m_retryCount;

    void process_rt_event_slots();
    void process_rt_event_signals();

signals:
    void audioThreadEventBufferFull(QString);
};

// use this function to access the tsar singleton pointer
Tsar& tsar();

#endif


//eof



