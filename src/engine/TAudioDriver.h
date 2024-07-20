/*
    Copyright (C) 2005-2006 Remon Sijrier 
 
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
 
    $Id: Driver.h,v 1.5 2007/03/19 11:18:57 r_sijrier Exp $
*/

#ifndef T_AUDIO_DRIVER_H
#define T_AUDIO_DRIVER_H

#include "defines.h"
#include "AudioDevice.h"
#include <memops.h>


#define DEFAULTDRIVERBUFFERSIZE  1024

#include <QList>
#include <QString>
#include <QObject>

class AudioDevice;
class AudioChannel;

class TAudioDriver : public QObject
{
    Q_OBJECT

public:
        TAudioDriver(AudioDevice* device);
        virtual ~TAudioDriver();

        virtual int _run_cycle();
        virtual int _read(nframes_t nframes);
        virtual int _write(nframes_t nframes);
        virtual int _null_cycle(nframes_t nframes);
        virtual int attach();
        virtual int detach();
        virtual int start();
        virtual int stop();
        virtual bool supports_software_channels() {return true;}
        virtual QString get_device_name();
        virtual QString get_device_longname();

        QList<AudioChannel* > get_capture_channels() const {return m_captureChannels;}
        QList<AudioChannel* > get_playback_channels() const {return m_playbackChannels;}

        AudioChannel* get_capture_channel_by_name(const QString& name);
        AudioChannel* get_playback_channel_by_name(const QString& name);

        virtual AudioChannel* add_playback_channel(const QString& chanName);
        virtual AudioChannel* add_capture_channel(const QString& chanName);

        virtual int remove_capture_channel(const QString& ) {return -1;}
        virtual int remove_playback_channel(const QString& ) {return -1;}


        ProcessCallback read;
        ProcessCallback write;
        RunCycleCallback run_cycle;


protected:
        AudioDevice*            m_device;
        QList<AudioChannel* >   m_captureChannels;
        QList<AudioChannel* >   m_playbackChannels;
        int             		m_dither{};
        dither_state_t*			m_ditherState{};
        trav_time_t 			m_periodUSecs{};
        trav_time_t 			m_lastWaitUsecond{};
        nframes_t               m_frameRate;
        nframes_t               m_framesPerCycle;
        nframes_t               m_captureFrameLatency{};
        nframes_t               m_playbackFrameLatency{};

signals:
        void errorMessage(const QString& message);

};


#endif

//eof

