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

#include "TInformUser.h"
#include "TAudioDevice.h"
#include "TChannelBuffer.h"

#include <QCoreApplication>
#include <QMessageBox>

#include "Debugger.h"


TInformUser& tInformUser()
{
        static TInformUser information;
        return information;
}


// A buffer can be made in any thread, and a failure from before TInformUser
// existed is passed while it is being made: the message goes through the
// application's event loop, where tInformUser() exists
static void tell_memory_lock_failure(TChannelBuffer::MemoryLockFailure failure)
{
    QMetaObject::invokeMethod(QCoreApplication::instance(), [failure]() {
#if defined(Q_OS_LINUX)
        if (failure == TChannelBuffer::MemoryLockFailure::NoPermission) {
            tInformUser().warning(TInformUser::tr(
                "Traverso has no permission to lock its audio buffers in memory. Now and then this can cause "
                "a dropout. Make your user a member of the audio group, or allow memlock in "
                "/etc/security/limits.conf, and log in again."));
        } else {
            tInformUser().warning(TInformUser::tr(
                "Traverso reached the limit on locked memory: not all of its audio buffers are locked in "
                "memory. Now and then this can cause a dropout. Raise memlock for your user in "
                "/etc/security/limits.conf, or make your user a member of the audio group, and log in again."));
        }
#else
        if (failure == TChannelBuffer::MemoryLockFailure::NoPermission) {
            tInformUser().warning(TInformUser::tr(
                "Traverso has no permission to lock its audio buffers in memory. Now and then this can cause "
                "a dropout."));
        } else {
            tInformUser().warning(TInformUser::tr(
                "Traverso reached the limit on locked memory: not all of its audio buffers are locked in "
                "memory. Now and then this can cause a dropout."));
        }
#endif
    }, Qt::QueuedConnection);
}

TInformUser::TInformUser()
{
    TChannelBuffer::set_memory_lock_failure_handler(tell_memory_lock_failure);
    connect(&audiodevice(), &TAudioDevice::newDriverSetupMessage, this, &TInformUser::audiodevice_message);
    connect(&audiodevice(), &TAudioDevice::audioThreadStalled, this, &TInformUser::audio_thread_stalled);
    connect(&audiodevice(), &TAudioDevice::audioThreadDead, this, &TInformUser::audio_thread_dead);
}


void TInformUser::information( const QString & mes )
{
        TInformUserData s;
        s.message = mes;
        s.type = INFO;
	PMESG("Information::information %s", qUtf8Printable(mes));
	emit message(s);
}

void TInformUser::warning( const QString & mes )
{
        TInformUserData s;
        s.message = mes;
        s.type = WARNING;
        PWARN(QString("Information::warning %1").arg(mes).toLatin1().data());
        emit message(s);
}


void TInformUser::critical( const QString & mes )
{
        TInformUserData s;
        s.message = mes;
        s.type = CRITICAL;
//	PERROR("Information::critical %s", qUtf8Printable(mes));
	emit message(s);
}

void TInformUser::audiodevice_message()
{
    auto messages = audiodevice().get_audio_driver_setup_messages();
    for (const auto &message : messages) {
        switch(message.severity) {
            case TAudioDevice::INFO: information(message.message);
            break;
            case TAudioDevice::WARNING: warning(message.message);
            break;
            case TAudioDevice::CRITICAL: critical(message.message);
            break;
            default: ;// do nothing;
        }
    }
}

void TInformUser::audio_thread_stalled(const QString& driverType)
{
    QMessageBox::critical(nullptr, tr("Traverso - Malfunction!"),
        tr("The %1 audio driver thread stopped processing, but Traverso didn't ask for it!\n"
           "Traverso switched to the 'Dummy' driver, so you can save your work.\n\n"
           "Potential issues why this can show up are:\n\n"
           "* You're not running with real time privileges! Please make sure this is setup properly.\n\n"
           "* The audio chipset isn't supported (completely), you probably have to turn off some of it's features.\n").arg(driverType),
        QMessageBox::Ok);
}

void TInformUser::audio_thread_dead()
{
    QMessageBox::critical(nullptr, tr("Traverso - Fatal!"),
                          tr("The Dummy audio driver stalled too, exiting application!"), QMessageBox::Ok);
    QCoreApplication::exit(-1);
}
