/*
Copyright (C) 2006 Remon Sijrier 

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

$Id: Tsar.cpp,v 1.4 2008/02/11 10:11:52 r_sijrier Exp $
*/

#include "Tsar.h"

#include "AudioDevice.h"

#include <QMetaMethod>
#include <QMessageBox>
#include <QCoreApplication>
#include <QThread>
#include <unistd.h>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"
#include "TAudioDeviceSetup.h"

/**
 * 	\class Tsar
 * 	\brief Tsar (Thread Save Add and Remove) is a singleton class to call  
 *		functions (both signals and slots) in a thread save way without
 *		using any mutual exclusion primitives (mutex)
 *
 */


void TsarThread::process_tsar_signals() {
    while(true) {
        // printf("calling tsar process_tsar_signals\n");
        tsar().process_rt_event_signals();
    }
}

/**
 * 
 * @return The Tsar instance. 
 */
Tsar& tsar()
{
	static Tsar ThreadSaveAddRemove;
	return ThreadSaveAddRemove;
}

Tsar::Tsar()
{
    m_blockingGuiThreadEventBuffer = new moodycamel::BlockingReaderWriterCircularBuffer<TsarEvent>(1024);
    m_blockingEmitEventSignalsInGuiThreadQueue = new moodycamel::BlockingReaderWriterCircularBuffer<TsarEvent>(1024);

    m_eventCounter = 0;
    m_retryCount = 0;

    auto tsarThread = new TsarThread;
    connect(tsarThread, SIGNAL(started()), tsarThread, SLOT(process_tsar_signals()));
    tsarThread->start();
    tsarThread->moveToThread(tsarThread);
}

Tsar::~ Tsar( )
{
}


/**
 * 	Use this function to add events to the event queue when 
 * 	called from the GUI thread.
 *
 *  Blocks (buzy waits) if the event buffer is full
 *
 *	Note: This function should be called ONLY from the GUI thread! 
 * @param event  The event to add to the event queue
 */
void Tsar::post_gui_event(const TsarEvent &event )
{
    Q_ASSERT_X(this->thread() == QThread::currentThread(), "Tsar::add_event", "Adding event from other then GUI thread!!");

    m_blockingGuiThreadEventBuffer->try_enqueue(std::move(event));

    m_eventCounter++;
}

/**
 * 	Use this function to add events to the event queue when
 * 	called from the audio processing (real time) thread
 *
 *	Note: This function should be called ONLY from the realtime audio thread and has a
 *	blocking behaviour if the event buffer is full, we don't want to lose events do we?
 *
 * @param event The event to add to the event queue
 */
void Tsar::post_rt_event(const TsarEvent &event )
{
    #if defined (THREAD_CHECK)
        Q_ASSERT_X(m_threadPointer != QThread::currentThread(), "Tsar::add_rt_event", "Adding event from NON-RT Thread!!");
    #endif

    // auto startTime = TTimeRef::get_nanoseconds_since_epoch();

    m_blockingEmitEventSignalsInGuiThreadQueue->try_enqueue(event);

    // auto totaltime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    // printf("post_rt_event took: %ld\n", totaltime);
}


//
//  Function called in RealTime AudioThread processing path
//
void Tsar::process_rt_event_slots( )
{
   TsarEvent event;
   // auto startTime = TTimeRef::get_nanoseconds_since_epoch();

   // Blocking queue
    while (m_blockingGuiThreadEventBuffer->try_dequeue(event)) {
        process_event_slot(event);
        // printf("Processed %s slot: %s, signal: %s\n", event.caller->metaObject()->className(),
        //        (event.slotindex >= 0) ? event.caller->metaObject()->method(event.slotindex).methodSignature().data() : "no_slot_supplied",
        //        (event.signalindex >= 0) ? event.caller->metaObject()->method(event.signalindex).methodSignature().data() : "so_signal_supplied");
        if (event.signalindex >= 0) {
            m_blockingEmitEventSignalsInGuiThreadQueue->try_enqueue(event);
        } else {
            --m_eventCounter;
        }
    }

    // auto totaltime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    // printf("process_rt_event_slots took: %ld\n", totaltime);
}

// Called by TsarThread which is allowed to block on the wait_dequeue()
void Tsar::process_rt_event_signals( )
{
    static TsarEvent event;

    m_blockingEmitEventSignalsInGuiThreadQueue->wait_dequeue(event);

    process_event_signal(event);

    --m_eventCounter;
    m_retryCount++;
	
    if (m_retryCount > 200)
	{
		if (audiodevice().get_driver_type() != "Null Driver") {
            QMessageBox::critical( nullptr,
				tr("Traverso - Malfunction!"), 
				tr("The Audiodriver Thread seems to be stalled/stopped, but Traverso didn't ask for it!\n"
				"This effectively makes Traverso unusable, since it relies heavily on the AudioDriver Thread\n"
				"To ensure proper operation, Traverso will fallback to the 'Null Driver'.\n"
				"Potential issues why this can show up are: \n\n"
				"* You're not running with real time privileges! Please make sure this is setup properly.\n\n"
				"* The audio chipset isn't supported (completely), you probably have to turn off some of it's features.\n"
				"\nFor more information, see the Help file, section: \n\n AudioDriver: 'Thread stalled error'\n\n"),
                QMessageBox::Ok);
            TAudioDeviceSetup ads;
            ads.driverType = "Null Driver";
            audiodevice().set_parameters(ads);
			m_retryCount = 0;
		} else {
            QMessageBox::critical( nullptr,
				tr("Traverso - Fatal!"), 
				tr("The Null AudioDriver stalled too, exiting application!"),
                QMessageBox::Ok);
			QCoreApplication::exit(-1);
		}
	}
	
	if (m_eventCounter <= 0) {
		m_retryCount = 0;
	}
}


/**
*	This function can be used to process the events 'slot' part.
*	Usefull when you have a Tsar event, but don't want/need to use tsar
*	to call the events slot in a thread save way
*
* @param event The TsarEvent to be processed 
*/
void Tsar::process_event_slot(const TsarEvent& event )
{
    Q_ASSERT(event.slotindex >= 0);

    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&event.argument)) };

    if ( ! (event.caller->qt_metacall(QMetaObject::InvokeMetaMethod, event.slotindex, _a) < 0) ) {
        qDebug("Tsar::process_event_slot failed (%s::%s)", event.caller->metaObject()->className(), event.caller->metaObject()->method(event.slotindex).methodSignature().data());
    }
}

/**
*	This function can be used to process the events 'signal' part.
*	Usefull when you have a Tsar event, but don't want/need to use tsar
*	to call the events signal in a thread save way
*
* @param event The TsarEvent to be processed 
*/
void Tsar::process_event_signal(const TsarEvent & event )
{
    Q_ASSERT(event.signalindex >= 0);

    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(&event.argument))};

    if ( ! (event.caller->qt_metacall(QMetaObject::InvokeMetaMethod, event.signalindex, _a) < 0) ) {
            qDebug("Tsar::process_event_signal failed (%s::%s)", event.caller->metaObject()->className(), event.caller->metaObject()->method(event.signalindex).methodSignature().data());
    }
}

/**
*	Convenience function. Calls both process_event_slot() and process_event_signal()
*
*	\sa process_event_slot() \sa process_event_signal()
*
*	Note: This function doesn't provide the thread safetyness you get with
*		the add_event() function!
*
* @param event The TsarEvent to be processed 
*/
void Tsar::process_event(const TsarEvent & event )
{
	process_event_slot(event);
	process_event_signal(event);
}

void Tsar::add_rt_event(QObject *cal, void* arg, const char* signalSignature)
{
    TsarEvent event;
    event.caller = cal;
    event.argument = arg;
    event.slotindex = -1;
    int retrievedsignalindex = cal->metaObject()->indexOfSignal(signalSignature);
    Q_ASSERT(retrievedsignalindex >= 0);
    event.signalindex = retrievedsignalindex;
    post_rt_event(event);
}

void Tsar::add_gui_event(QObject *caller, void *arg, const char *slotSignature, const char *signalSignature)
{
    PENTER;
    TsarEvent event;
    prepare_event(event, caller, arg, slotSignature, signalSignature);
    post_gui_event(event);
}

/**
 */
void Tsar::prepare_event(TsarEvent &event, QObject* caller, void* argument, const char* slotSignature, const char* signalSignature )
{
    PENTER3;
    event.caller = caller;
    event.argument = argument;

    event.slotindex = caller->metaObject()->indexOfMethod(slotSignature);
    event.signalindex = caller->metaObject()->indexOfMethod(signalSignature);
}

//eof

