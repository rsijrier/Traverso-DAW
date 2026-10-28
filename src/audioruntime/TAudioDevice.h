/*
Copyright (C) 2005-2010 Remon Sijrier

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

#pragma once

#include <QObject>
#include <QList>
#include <QVector>
#include <QHash>
#include <QStringList>
#include <QByteArray>
#include <QTimer>
#include <QElapsedTimer>
#include "TAudioThreadWatchdog.h"
#include <QVariant>


#include "TAudioChannelConfiguration.h"
#include "TRtList.h"
#include "TAudioBusConfiguration.h"
#include "TAudioDeviceSetup.h"
#include "TTimeRef.h"
#include "TTransportControl.h"
#include "TProcessContext.h"
#include "TTypes.h"
#include "TRealTime.h"

class TAudioDeviceThread;
class TAudioDriver;
class TAudioDeviceClient;
class AudioChannel;
class AudioBus;
#if defined (JACK_SUPPORT)
class TJackDriver;
#endif

#if defined (COREAUDIO_SUPPORT)
class TCoreAudioDriver;
#endif

#if defined (PIPEWIRE_SUPPORT)
class TPipeWireDriver;
#endif

// A member function bound to a function pointer, called like one. On the
// audio processing path (RealtimeSafe, the default) the bound function has to
// be T_REALTIME_SAFE: one that is not does not bind (noexcept, every
// compiler), and Clang checks the call as real-time safe
// (doc/design/realtime-audio-path.md).
template <typename Signature, bool RealtimeSafe = true>
class TRealTimeCallBack;

template <typename ReturnType, typename... Args, bool RealtimeSafe>
class TRealTimeCallBack<ReturnType(Args...), RealtimeSafe> {
public:
    TRealTimeCallBack() : m_instance(nullptr), m_stub(nullptr) {}

    // Safely binds any member function at compile time
    template <typename T, ReturnType(T::*Method)(Args...) T_REALTIME_SAFE_IF(RealtimeSafe)>
    static TRealTimeCallBack from_method(T* instance) {
        TRealTimeCallBack d;
        d.m_instance = instance;
        d.m_stub = [](void* obj_ptr, Args... args) T_REALTIME_SAFE_IF(RealtimeSafe) -> ReturnType {
            return (static_cast<T*>(obj_ptr)->*Method)(args...);
        };
        return d;
    }

    // Invocation operator: executes exactly like a raw function pointer
    ReturnType operator()(Args... args) const T_REALTIME_SAFE_IF(RealtimeSafe) {
        return (*m_stub)(m_instance, args...);
    }

    explicit operator bool() const { return m_stub != nullptr; }

private:
    void* m_instance;
    ReturnType (*m_stub)(void*, Args...) T_REALTIME_SAFE_IF(RealtimeSafe);
};

using TAudioDriverReadWriteCallBack = TRealTimeCallBack<int(nframes_t)>;
using TProcessCallBack              = TRealTimeCallBack<int(TProcessContext&)>;
// The driver's loop: its clock waits for the device, by design not real-time
// safe; the path starts at TAudioDevice::run_cycle()
using TRunCycleCallBack             = TRealTimeCallBack<int(), false>;
using TTransportControlCallBack      = TRealTimeCallBack<int(TTransportControl*)>;

struct TAudioDriverSetupMessage
{
    QString message;
    QString driverType;
    int     severity;
    qint64  createdOn; // msecs since epoch
};

class TAudioDevice : public QObject
{
    Q_OBJECT

public:
    enum {
        INFO,
        WARNING,
        CRITICAL,
        DRIVER_SETUP_INFO,
        DRIVER_SETUP_SUCCESS,
        DRIVER_SETUP_FAILURE,
        DRIVER_SETUP_WARNING
    };

    void set_parameters(TAudioDeviceSetup ads);
    void set_free_wheeling(bool freeWheeling);
    // Any thread; a cycle uses the value it copied at its start
    bool running_real_time() const {return m_isRealTime.load(std::memory_order_relaxed);}

    void add_client(TAudioDeviceClient* client);
    // Returns once the audio thread no longer uses client, so its owner may
    // delete it (is_connected() is false then); after a second without that,
    // client stays connected
    void remove_client(TAudioDeviceClient* client);

    // GUI thread: waits until the audio thread applied every change posted so
    // far, e.g. before deleting what it no longer reaches. False when that did
    // not happen within timeoutMilliseconds.
    bool wait_until_changes_applied(int timeoutMilliseconds);

    void transport_start(TAudioDeviceClient* client);
    void transport_stop(TAudioDeviceClient* client, const TTimeRef& location);
    int transport_locate(TAudioDeviceClient* client, const TTimeRef &location);

    TAudioDeviceSetup get_device_setup() {return m_setup;}

    AudioChannel* create_channel(const QString& name, uint channelNumber, int type);
    AudioChannel* get_playback_channel_by_name(const QString& name);
    AudioChannel* get_capture_channel_by_name(const QString& name);

    void delete_channel(AudioChannel* channel);

    QStringList get_capture_channel_names() const;
    QStringList get_playback_channel_names() const;

    QList<AudioChannel*> get_channels() const;
    QList<AudioChannel*> get_playback_channels() const;
    QList<AudioChannel*> get_capture_channels() const;
    int add_jack_channel(AudioChannel* channel);
    void remove_jack_channel(AudioChannel* channel);

    QString get_device_name() const;
    QString get_device_longname() const;
    QString get_driver_type() const;
    QString get_driver_information() const;
    bool is_driver_loaded() const {return m_driver ? true : false;}

    QStringList get_available_drivers() const;
    QList<TAudioDriverSetupMessage> get_audio_driver_setup_messages() const {
        return m_audioDriverSetupMessages.values();
    }

    uint get_sample_rate() const T_REALTIME_SAFE;
    uint get_bit_depth() const;
    // From the start of a process cycle until the first frame of its buffer
    // leaves the output, as the driver knows it. Any thread
    TTimeRef get_playback_latency() const;
    // The driver's capture latency (how long ago the first frame of a
    // cycle's input came in) and playback latency (until the first frame of
    // its output leaves), in frames of the device's rate. Together they are
    // how late a recorded frame comes in against what was heard when it was
    // played (TRecordingLatency). Any thread
    nframes_t get_capture_latency_frames() const;
    nframes_t get_playback_latency_frames() const;

    /**
	 * 
	 * @return The period buffer size, as used by the Audio Driver.
	 */
    nframes_t get_buffer_size() const
    {
        return m_bufferSize;
    }

    QString get_driver_latency_in_ms();

    void show_descriptors();
    void set_driver_properties(QHash<QString, QVariant>& properties);

    int shutdown();

    float get_cpu_time();

    // The application owns its device (Traverso.cpp), a test its own; one
    // at a time, which audiodevice() reaches while the callers move to a
    // reference (T58)
    TAudioDevice();
    ~TAudioDevice();
    TAudioDevice(const TAudioDevice&) = delete;
    TAudioDevice& operator=(const TAudioDevice&) = delete;

private:
    friend TAudioDevice& audiodevice();
    static TAudioDevice* s_bridge;

    friend class TAlsaDriver;
    friend class TPortAudioDriver;
    friend class TAudioDriver;
    friend class TAudioDeviceThread;
#if defined (COREAUDIO_SUPPORT)
    friend class TCoreAudioDriver;
#endif
#if defined (PIPEWIRE_SUPPORT)
    friend class TPipeWireDriver;
#endif
    TRtList<TAudioDeviceClient> m_clients;

    TProcessContext    m_processCallBackData;
    TTransportControl   m_transportControl;

    TAudioDeviceSetup   m_setup;
    TAudioDeviceSetup   m_fallBackSetup;
    TAudioDeviceSetup   fallback_setup() const;
    TAudioDriver* 		m_driver;
    TAudioDeviceThread* 	m_audioThread;

    QList<AudioChannel* >               m_audioChannels;
    QList<TAudioBusConfiguration>       m_busConfigs;
    QList<TAudioChannelConfiguration>   m_channelConfigs;
    QStringList		m_availableDrivers;

    QHash<QString, QVariant> m_driverProperties;
    QMap<int, TAudioDriverSetupMessage> m_audioDriverSetupMessages;

    QTimer			m_xrunResetTimer;

    // Watches whether the audio thread keeps completing its cycles
    std::atomic<quint64>    m_cycleCount{0};
    TAudioThreadWatchdog    m_watchdog;
    QTimer                  m_watchdogTimer;
    QElapsedTimer           m_watchdogClock;
#if defined (JACK_SUPPORT)
    QTimer			jackShutDownChecker;
    TJackDriver* slaved_jack_driver();
    friend class TJackDriver;
#endif

    // The audio thread adds a cycle's time and takes off its waits on ring
    // buffers; the GUI takes and zeroes it (exchange(0))
    std::atomic<trav_time_t>    m_processCallBackCpuTime;
    // Set by xrun() on a storm, taken by the GUI's watchdog timer
    std::atomic<bool>           m_xrunStorm{false};
    std::atomic<bool>           m_runAudioThread;
    // Written by the GUI (free-wheeling) or the JACK freewheel callback;
    // run_cycle() copies it into the process context at the start of a cycle
    std::atomic<bool>           m_isRealTime;
    trav_time_t		m_cycleStartTime;
    trav_time_t		m_lastCpuReadTime;
    uint 			m_bufferSize;
    uint 			m_rate;
    uint			m_bitdepth;
    // The audio thread counts, the GUI's timer and set_parameters() reset
    std::atomic<uint>   m_xrunCount;
    QString			m_driverType;
    QString			m_ditherShape;


    // The audio processing path starts here, after the driver's clock
    int run_cycle(nframes_t nframes, float delayed_usecs) T_REALTIME_SAFE;
    int run_one_cycle(nframes_t nframes, float delayed_usecs) T_REALTIME_SAFE;

    int _run_cycle(); // called by AudioDeviceThread;
    int start_driver();

    void create_driver();
    int setup_driver();
    int transport_control(TTransportControl* state);

    void driver_changed_free_wheel_mode();

    void set_buffer_size(uint size);
    void set_sample_rate(uint rate);
    void set_bit_depth(uint depth);
    void delay(float delay);

    void set_transport_cycle_start_time(trav_time_t time)
    {
        m_cycleStartTime = time;
    }

    void set_transport_cycle_end_time(trav_time_t time)
    {
        m_processCallBackCpuTime.fetch_add(time - m_cycleStartTime, std::memory_order_relaxed);
    }

    TAudioDriver* get_driver() const {return m_driver;}

    void xrun();

    inline bool run_audio_thread() const {return m_runAudioThread.load();}

    QVariant get_driver_property(const QString& property, const QVariant& defaultValue);

signals:
    /**
	 *      The stopped() signal is emited just before the AudioDeviceThread will be stopped.
	 *	Connect this signal to all Objects that have a pointer to an AudioBus (For example a VU meter),
     *	since all he Buses will be deleted, and new ones created when the TAudioDevice re-inits
	 *	the AudioDriver.
	 */
    void stopped();

    /**
	 *      The started() signal is emited ones the AudioThread and AudioDriver have been succesfully
	 *	setup.
	 */
    void started();

    /**
	 *      The driverParamsChanged() signal is emited just before the started() signal, you should 
	 *	connect all objects to this signal who need a pointer to one of the AudioBuses supplied by 
     *	the TAudioDevice!
	 */
    void driverParamsChanged();

    // Emitted when the TAudioDriver has changed it's latency for either the playback or capture stream
    void latencyChanged();


    /**
	 *        Connect this signal to any Object who need to be informed about buffer under/overruns
	 */
    void bufferUnderRun();

    /**
	 *        This signal will be emited after succesfull Client removal from within the GUI Thread!
     * @param  The Client \a client which as been removed from the TAudioDevice
	 */
    void audioDeviceClientRemoved(TAudioDeviceClient*);
    void audioDeviceClientAdded(TAudioDeviceClient*);

    void xrunStormDetected();

    // The audio thread of driverType stopped completing its cycles, the
    // device switched to the Dummy driver so the user can save their work
    void audioThreadStalled(const QString& driverType);
    // The audio thread of the Dummy driver stalled too
    void audioThreadDead();

    void newDriverSetupMessage();

    void freeWheelingChanged();

private slots:
    void audiothread_finished();
    void switch_to_null_driver();
    void reset_xrun_counter() {m_xrunCount.store(0, std::memory_order_relaxed);}
    void check_jack_shutdown();
    void check_audio_thread_progress();
    void driver_setup_message(const QString &driver, const QString &message, int severity, trav_time_t creatonOn = TTimeRef::get_milliseconds_since_epoch());
};


// use this function to get the audiodevice object
// The bridge to the one device while callers still reach it this way (T58
// step 2): the device that exists now. Not for the audio thread: a cycle has
// TProcessContext, a driver m_device.
TAudioDevice& audiodevice();



//eof
