/*
Copyright (C) 2005-2024 Remon Sijrier

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

#include "TAudioDevice.h"
#include "TDenormals.h"
#include "TAudioDeviceThread.h"
#include "TAudioThreadMessageQueue.h"

#if defined (ALSA_SUPPORT)
#include "TAlsaDriver.h"
#endif

#if defined (JACK_SUPPORT)
#include "TJackDriver.h"
#endif

#if defined (PORTAUDIO_SUPPORT)
#include "TPortAudioDriver.h"
#endif

#if defined (COREAUDIO_SUPPORT)
#include "TCoreAudioDriver.h"
#endif

#if defined (PIPEWIRE_SUPPORT)
#include "TPipeWireDriver.h"
#endif


#include "TAudioDriver.h"
#include "TAudioDeviceClient.h"
#include "TRtTransaction.h"
#include "TClock.h"
#include "AudioChannel.h"
#include "TAudioThreadMessageQueue.h"

//#include <sys/mman.h>
#include <QDebug>
#include <QThread>
#include "Debugger.h"


/*! 	\class AudioDevice
    \brief An Interface to the 'real' audio device, and the hearth of the libtraversoaudiobackend

    The application owns one device (Traverso.cpp; a test its own); audiodevice() reaches it while
    callers move to a reference (T58). You need to first initialize the 'device' by
    calling AudioDevice::set_parameters(int rate, nframes_t bufferSize, QString driverType);
    This will initialize the real audiodevice in case of the Alsa driver, or connect to the jack deamon.
    In the latter case, the rate and bufferSize don't do anything, since they are provided by the jack itself

        This class and/or related classes depend on RingBuffer, TSMP and FastDelegate which are found in 'src/common' directory.
    The signal/slot feature as supplied by Qt is also used, which makes the Qt dependency a bit deeper, though
    it shouldn't be to hard to get rid of it if you like to use the libtraversoaudiobackend in an application not
    using Qt, or if you don't want a dependency to Qt.

    Using the audiobackend in an application is as simple as:

    \code
    #include <AudioDevice.h>

    main()
    {
        myApp = new MyApp();
        myApp->execute();
        return;
    }

    MyApp::MyApp()
        : QApplication
    {
        setup_audiobackend();
        connect_to_audiodevice();
    }


    void MyApp::setup_audiobackend()
    {
                AudioDeviceSetup ads;
                ads.driverType = "ALSA";
                ads.rate = 48000;
                ads.bufferSize = 512;
                audiodevice().set_parameters(ads);
    }
    \endcode


    The AudioDevice instance now has set up it's own audio thread, or uses the one created by jack.
    This thread will continuously run, and process the callback functions of the registered Client's

    Connecting your application to the audiodevice is done by creating an instance of Client, and
    setting the right callback function. The Client is added to the audiodevice in a thread save way,
    without using any locking mechanisms.

    \code
    void MyApp::connect_to_audiodevice()
    {
        m_client = new Client("MyApplication");
        m_client->set_process_callback( TProcessCallBack(this, &MyApp::process) );
        audiodevice().add_client(m_client);
    }
    \endcode

    Finally, we want to do some processing in the process callback, e.g.

    \code
    int MyApp::process(nframes_t nframes)
    {
        AudioBus* captureBus = audiodevice().get_capture_bus("Capture 1");
        AudioBus* playbackBus = audiodevice().get_playback_bus("Playback 1");

        // Just copy the captured audio to the playback buses.
        for (int i=0; i<captureBuses->get_channel_count(); ++i) {
            memcpy(captureBus->get_channel(i)->get_buffer(nframes), playbackBus->get_channel(i)->get_buffer(nframes), nframes);
        }

        return 1;
    }
    \endcode

    */

TAudioDevice* TAudioDevice::s_bridge = nullptr;

TAudioDevice& audiodevice()
{
    Q_ASSERT_X(TAudioDevice::s_bridge, "audiodevice()", "no device made yet, or already gone");
    return *TAudioDevice::s_bridge;
}

TAudioDevice::TAudioDevice()
{
    Q_ASSERT_X(!s_bridge, "TAudioDevice", "one device at a time");
    s_bridge = this;

    m_runAudioThread.store(false);
    m_driver = nullptr;
    m_audioThread = nullptr;
    m_bufferSize = 1024;
    m_rate = 0;
    m_bitdepth = 0;
    m_xrunCount.store(0);
    m_processCallBackCpuTime.store(0);
    m_cycleStartTime = {};
    m_lastCpuReadTime = {};
    m_isRealTime.store(true);

    m_driverType = tr("No Driver Loaded");

    m_fallBackSetup.set_driver_type("Dummy");


#if defined (ALSA_SUPPORT)
    m_availableDrivers << "ALSA";
#endif

#if defined (COREAUDIO_SUPPORT)
    m_availableDrivers << "CoreAudio";
#endif

#if defined (PIPEWIRE_SUPPORT)
    m_availableDrivers << "PipeWire";
#endif

#if defined (JACK_SUPPORT)
    m_availableDrivers << "Jack";
#endif

#if defined (PORTAUDIO_SUPPORT)
    m_availableDrivers << "PortAudio";
#endif


    m_availableDrivers << "Dummy";

    connect(this, &TAudioDevice::xrunStormDetected, this, &TAudioDevice::switch_to_null_driver);
    connect(&m_xrunResetTimer, &QTimer::timeout, this, &TAudioDevice::reset_xrun_counter);

    m_xrunResetTimer.start(30000);

    m_watchdogClock.start();
    m_watchdogTimer.setInterval(500);
    connect(&m_watchdogTimer, &QTimer::timeout, this, &TAudioDevice::check_audio_thread_progress);
}

TAudioDevice::~TAudioDevice()
{
    PENTERDES;

    shutdown();

    delete m_audioThread;
    for (AudioChannel* audioChannel : m_audioChannels) {
        delete audioChannel;
    }

    if (s_bridge == this) {
        s_bridge = nullptr;
    }
}

/**
 *
 * Not yet implemented
 */
void TAudioDevice::show_descriptors( )
{
    // Needs to be implemented
}

void TAudioDevice::set_buffer_size( nframes_t size )
{
    Q_ASSERT(size > 0);
    m_bufferSize = size;

    for (auto chan : m_audioChannels) {
        chan->set_buffer_size(m_bufferSize);
    }

}

void TAudioDevice::set_sample_rate( uint rate )
{
    m_rate = rate;
    m_processCallBackData.set_sample_rate(m_rate);
}

void TAudioDevice::set_bit_depth( uint depth )
{
    m_bitdepth = depth;
}

int TAudioDevice::start_driver()
{
    Q_ASSERT(m_driver);

    return m_driver->start();
}


int TAudioDevice::_run_cycle()
{
    int result;
    if (running_real_time()) {
        result = m_driver->run_cycle();
    } else {
        result = m_driver->TAudioDriver::run_cycle();
    }

    return result;
}

int TAudioDevice::run_cycle( nframes_t nframes, float delayed_usecs ) T_REALTIME_SAFE
{
    nframes_t left;

    // Every cycle: a driver that made a new thread, or a plugin that changed
    // MXCSR, gets flush-to-zero back before anything is processed
    TDenormals::enforce();
    // The mode for this cycle's processing and driver read/write: the context
    // stays a plain struct only this thread writes. A blocking driver's loop
    // chose its wait before this (_run_cycle()): on a switch that one cycle
    // can differ
    m_processCallBackData.set_real_time(m_isRealTime.load(std::memory_order_relaxed));

    TLog::set_audio_cycle(m_cycleCount.load(std::memory_order_relaxed) + 1);

    if (nframes != m_bufferSize) {
        TLOG_RT_W("audio", "late driver wakeup: {} frames to process instead of {}", nframes, m_bufferSize);
    }

    // Apply what the GUI posted before processing, so the cycle sees the
    // current clients and state. A driver that just started then never
    // processes a client whose removal was posted before it started
    tsmp().process_posted_gui_events();

    // The sheets publish their transport position with this time
    m_processCallBackData.set_cycle_start_nanoseconds(TClock::now_nanoseconds());

    /* run as many cycles as it takes to consume nframes (Should be 1 cycle!!)*/
    for (left = nframes; left >= m_bufferSize; left -= m_bufferSize) {
        if (run_one_cycle (m_bufferSize, delayed_usecs) < 0) {
            TLOG_RT_E("audio", "cycle execution failure, exiting");
            return -1;
        }
    }

    // Wakes the message queue worker thread, which runs the callbacks of
    // the changes applied this cycle. The GUI follows the transport with
    // TFrameClock, not with an event per cycle
    tsmp().post_to_gui_thread(TAudioThreadMessageQueueEvent{});

    // Only the audio thread writes it. Release: whoever sees the new count
    // also sees the changes applied at the start of this cycle
    m_cycleCount.store(m_cycleCount.load(std::memory_order_relaxed) + 1, std::memory_order_release);

    return 1;
}

int TAudioDevice::run_one_cycle( nframes_t nframes, float  ) T_REALTIME_SAFE
{

    if (m_processCallBackData.get_is_real_time() && m_driver->read(nframes) < 0) {
        TLOG_RT_E("audio", "driver read failed");
        return -1;
    }

    for (TAudioDeviceClient* client : m_clients.rt_items()) {
        m_processCallBackData.set_nframes_to_process(nframes);
        client->process(m_processCallBackData);
        auto ringBufferReadTime = m_processCallBackData.get_ringbuffers_read_write_wait_time();
        // printf("processing wait time (micro seconds): %ld\n", ringBufferReadTime / 1000);
        // Waiting on a ring buffer is not load
        m_processCallBackCpuTime.fetch_sub(ringBufferReadTime, std::memory_order_relaxed);
    }

    if (m_processCallBackData.get_is_real_time() && m_driver->write(nframes) < 0) {
        TLOG_RT_E("audio", "driver write failed");
        return -1;
    }


    return 0;
}

void TAudioDevice::delay( float  )
{
}


/**
 * This function is used to initialize the TAudioDevice's audioThread with the supplied
 * rate, bufferSize, channel/bus config, and driver type. In case the TAudioDevice allready was configured,
 * it will stop the TAudioDeviceThread and emits the stopped() signal,
 * re-inits the AlsaDriver with the new paramaters, when succesfull emits the driverParamsChanged() signal,
 * restarts the TAudioDeviceThread and emits the started() signal
 *
 * @param TAudioDeviceSetup Contains all parameters the TAudioDevice needs
 */
void TAudioDevice::set_parameters(TAudioDeviceSetup ads)
{
    PENTER;

    shutdown();

    m_rate = ads.get_sample_rate();
    m_bufferSize = ads.get_buffer_size();
    // A new driver: a storm of the old one is over
    m_xrunCount.store(0);
    m_xrunStorm.store(false);
    m_ditherShape = ads.get_dither_shape();
    //        if (!(ads.driverType == "Dummy")) {
    m_setup = ads;
    m_driverType = m_setup.get_driver_type();
    //        }


    create_driver();

    if (m_driver) {
        connect(m_driver, &TAudioDriver::latencyChanged, this, &TAudioDevice::latencyChanged);
        connect(m_driver, &TAudioDriver::driverSetupMessage, this,
            [this](const QString& driver, const QString& message, int severity) {
                driver_setup_message(driver, message, severity);
            });
        if (setup_driver() > 0) {
            m_driverType = m_setup.get_driver_type();
            m_driver->attach();
        } else {
            TLOG_W("device", "failed to set up driver {}, falling back to the Dummy driver", m_setup.get_driver_type());
            delete m_driver;
            m_driver = nullptr;
            set_parameters(fallback_setup());
            return;
        }
    } else {
        set_parameters(fallback_setup());
        return;
    }

    TLOG_I("device", "driver={} device={} rate={} buffer={} realtime={}", m_driverType, m_setup.get_card_device(), m_rate,
           m_bufferSize, m_driver->is_realtime_capable());
    emit driverParamsChanged();

    m_runAudioThread.store(true);

    if (m_driver->runs_in_blocking_mode()) {

        TLOG_I("device", "starting the audio thread for driver {}, {} Hz, {} frames", m_driverType, m_rate, m_bufferSize);


        bool realTime = false;
        if (!m_audioThread) {
            if (m_driver->is_realtime_capable()) {
                realTime = true;
            }

            m_audioThread = new TAudioDeviceThread(this, realTime);
        }

        // m_cycleStartTime/EndTime are set before/after the first cycle.
        // to avoid a "100%" cpu usage value during audioThread startup, set the
        // m_cycleStartTime here!
        m_cycleStartTime = TTimeRef::get_nanoseconds_since_epoch();

        // When the audiothread fails for some reason we catch it in audiothread_finished()
        // by connecting the finished signal of the audio thread!
        connect(m_audioThread, &QThread::finished, this, &TAudioDevice::audiothread_finished);

        // Start the audio thread, the driver->start() will be called from there!!
        m_audioThread->start();

    } else {
        if (m_driver->start() == -1) {
            // PortAudio driver failed to start, fallback to Dummy Driver:
            set_parameters(fallback_setup());
            return;
        }

#if defined (JACK_SUPPORT)
        if (ads.get_driver_type() == "Jack") {

            connect(&jackShutDownChecker, &QTimer::timeout, this, &TAudioDevice::check_jack_shutdown);
            jackShutDownChecker.start(500);
        }
#endif

#if defined (PIPEWIRE_SUPPORT)
        if (ads.get_driver_type() == "PipeWire") {
            TPipeWireDriver* pwDriver = qobject_cast<TPipeWireDriver*>(m_driver);
            if (pwDriver) {
                connect(pwDriver, &TPipeWireDriver::pipewireShutDown, this, [this]() {
                    TLOG_W("device", "PipeWire shut down");
                    driver_setup_message("PipeWire", tr("The PipeWire server has been shutdown!"), CRITICAL);
                    delete m_driver;
                    m_driver = nullptr;
                    set_parameters(fallback_setup());
                }, Qt::QueuedConnection);
            }
        }
#endif
    }

    m_watchdog.restart(m_cycleCount.load(std::memory_order_relaxed), m_watchdogClock.elapsed());
    m_watchdogTimer.start();

    emit started();
}

void TAudioDevice::set_free_wheeling(bool freeWheeling)
{
    if (!m_driver) {
        return;
    }

    if (m_driver->supports_free_wheeling()) {
        if (freeWheeling) {
            m_driver->start_free_wheeling();
        } else {
            m_driver->stop_free_wheeling();
        }
        return;
    }


    // FIXME Freewheeling on current drivers that do not support
    // freewheeling natively is working somewhat on ALSA but not PortAudio
    if (freeWheeling) {
        m_driver->stop();
    } else {
        m_driver->start();
    }

    // The next cycle takes it over (run_cycle())
    m_isRealTime.store(!freeWheeling);

    emit freeWheelingChanged();
}

void TAudioDevice::driver_changed_free_wheel_mode()
{
    Q_ASSERT(m_driver && m_driver->supports_free_wheeling());

    // The JACK freewheel callback's thread; the next cycle takes it over
    m_isRealTime.store(!m_driver->is_free_wheeling());

    emit freeWheelingChanged();

}

void TAudioDevice::create_driver()
{
    Q_ASSERT(!m_driver);
    QString driverType = m_setup.get_driver_type();

#if defined (JACK_SUPPORT)
    if (driverType == "Jack") {
        m_driver = new TJackDriver(this);
        return;
    }
#endif

#if defined (ALSA_SUPPORT)
    if (driverType == "ALSA") {
        m_driver =  new TAlsaDriver(this);
        return;
    }
#endif

#if defined (PORTAUDIO_SUPPORT)
    if (driverType == "PortAudio") {
        m_driver = new TPortAudioDriver(this);
        return;
    }
#endif

#if defined (COREAUDIO_SUPPORT)
    if (driverType == "CoreAudio") {
        m_driver = new TCoreAudioDriver(this);
        return;
    }
#endif

#if defined (PIPEWIRE_SUPPORT)
    if (driverType == "PipeWire") {
        m_driver = new TPipeWireDriver(this);
        return;
    }
#endif


    if (driverType == "Dummy") {
        TLOG_I("device", "creating the Dummy driver");
        m_driver = new TAudioDriver(this);
        driver_setup_message("Dummy Driver", tr("Started successfully"), TAudioDevice::DRIVER_SETUP_SUCCESS);
        return;
    }
}


int TAudioDevice::setup_driver()
{
    Q_ASSERT(m_driver);

    QString driverType = m_setup.get_driver_type();
    bool capture = m_setup.get_capture();
    bool playback = m_setup.get_playback();
    QString cardDevice = m_setup.get_card_device();

#if defined (JACK_SUPPORT)
    if (driverType == "Jack") {
        TJackDriver* jackDriver = qobject_cast<TJackDriver*>(m_driver);
        if (jackDriver && jackDriver->setup(m_setup.get_jack_channels(), m_setup.get_project_name()) < 0) {
            driver_setup_message("AudioDevice", tr("Failed to setup the Jack Driver"), DRIVER_SETUP_FAILURE);
            return -1;
        }
        return 1;
    }
#endif

#if defined (PIPEWIRE_SUPPORT)
    if (driverType == "PipeWire") {
        TPipeWireDriver* pwDriver = qobject_cast<TPipeWireDriver*>(m_driver);
        if (pwDriver && pwDriver->setup(capture, playback, cardDevice) < 0) {
            driver_setup_message("AudioDevice", tr("Failed to setup the PipeWire Driver"), DRIVER_SETUP_FAILURE);
            return -1;
        }
        return 1;
    }
#endif

#if defined (ALSA_SUPPORT)
    if (driverType == "ALSA") {
        TAlsaDriver* alsaDriver = qobject_cast<TAlsaDriver*>(m_driver);
        if (alsaDriver && alsaDriver->setup(capture,playback, cardDevice, m_ditherShape) < 0) {
            driver_setup_message("AudioDevice", tr("Failed to setup the ALSA Driver"), DRIVER_SETUP_FAILURE);
            return -1;
        }
        return 1;
    }
#endif

#if defined (PORTAUDIO_SUPPORT)
    if (driverType == "PortAudio") {
        TPortAudioDriver* paDriver = qobject_cast<TPortAudioDriver*>(m_driver);
        if (paDriver && paDriver->setup(capture, playback, cardDevice) < 0) {
            driver_setup_message("AudioDevice", tr("Failed to setup the PortAudio Driver"), DRIVER_SETUP_FAILURE);
            return -1;
        }
        return 1;
    }
#endif

#if defined (COREAUDIO_SUPPORT)
    if (driverType == "CoreAudio") {
        TCoreAudioDriver* coreAudioDriver = dynamic_cast<TCoreAudioDriver*>(m_driver);
        if (coreAudioDriver && coreAudioDriver->setup(capture, playback, cardDevice) < 0) {
            driver_setup_message("CoreAudio", tr("Failed to initialize the CoreAudio driver"), DRIVER_SETUP_FAILURE);
            return -1;
        }
        return 1;
    }
#endif


    if (driverType == "Dummy") {
        // nothing to do for the Dummy Driver here
        return 1;
    }

    return -1;
}


/**
 * Stops the TAudioDevice's AudioThread, free's any related memory.
 
 * Use this to properly shut down the TAudioDevice on application exit,
 * or to explicitely release the real 'audiodevice'.
 
 * Use set_parameters() to reinitialize the audiodevice if you want to use it again.
 *
 * @return 1 on succes, 0 on failure
 */
int TAudioDevice::shutdown( )
{
    PENTER;
    int r = 1;

    emit stopped();

    m_watchdogTimer.stop();
    m_runAudioThread.store(false);

    if (m_audioThread) {
        disconnect(m_audioThread, &QThread::finished, this, &TAudioDevice::audiothread_finished);

        // Wait until the audioThread has finished execution. One second
        // should do, if it's still running then, the thread must have gone wild or something....
        if (m_audioThread->isRunning()) {
            TLOG_I("device", "stopping the audio thread");
            r = m_audioThread->wait(1000);
        }

        delete m_audioThread;
        m_audioThread = nullptr;
    }


    if (m_driver) {
        m_driver->stop();

        QList<AudioChannel*> channels = m_driver->get_capture_channels();
        channels.append(m_driver->get_playback_channels());
        foreach(AudioChannel* chan, channels) {
            m_audioChannels.removeAll(chan);
        }

        delete m_driver;
        m_driver = nullptr;
    }

    return r;
}

QStringList TAudioDevice::get_capture_channel_names() const
{
    QStringList names;
    foreach(AudioChannel* chan, m_driver->get_capture_channels()) {
        names.append(chan->get_name());
    }
    return names;
}

QStringList TAudioDevice::get_playback_channel_names() const
{
    QStringList names;
    foreach(AudioChannel* chan, m_driver->get_playback_channels()) {
        names.append(chan->get_name());
    }
    return names;
}

QList<AudioChannel*> TAudioDevice::get_channels() const
{
    QList<AudioChannel*> channels;
    if (!m_driver) {
        return channels;
    }

    channels.append(m_driver->get_capture_channels());
    channels.append(m_driver->get_playback_channels());

    return channels;
}

QList<AudioChannel*> TAudioDevice::get_capture_channels() const
{
    QList<AudioChannel*> channels;
    if (!m_driver) {
        return channels;
    }

    return m_driver->get_capture_channels();
}

QList<AudioChannel*> TAudioDevice::get_playback_channels() const
{
    QList<AudioChannel*> channels;
    if (!m_driver) {
        return channels;
    }

    return m_driver->get_playback_channels();
}

int TAudioDevice::add_jack_channel(AudioChannel *channel)
{
#if defined (JACK_SUPPORT)
    TJackDriver* jackdriver = qobject_cast<TJackDriver*>(m_driver);
    if (!jackdriver) {
        return -1;
    }

    jackdriver->add_channel(channel);

    return 1;
#else
    Q_UNUSED(channel);
#endif

    return -1;
}

void TAudioDevice::remove_jack_channel(AudioChannel *channel)
{
#if defined (JACK_SUPPORT)
    TJackDriver* jackdriver = qobject_cast<TJackDriver*>(m_driver);
    if (!jackdriver) {
        return;
    }

    TLOG_D("device", "removing channel from the JACK driver");
    jackdriver->remove_channel(channel);
#else
    Q_UNUSED(channel);
#endif
}

AudioChannel* TAudioDevice::get_capture_channel_by_name(const QString &name)
{
    if (!m_driver) {
        return nullptr;
    }
    return m_driver->get_capture_channel_by_name(name);
}


AudioChannel* TAudioDevice::get_playback_channel_by_name(const QString &name)
{
    if (!m_driver) {
        return nullptr;
    }
    return m_driver->get_playback_channel_by_name(name);
}

AudioChannel* TAudioDevice::create_channel(const QString& name, uint channelNumber, int type)
{
    AudioChannel* chan = new AudioChannel(name, channelNumber, type, m_bufferSize);
    m_audioChannels.append(chan);
    return chan;
}

void TAudioDevice::delete_channel(AudioChannel* channel)
{
    m_audioChannels.removeAll(channel);
    delete channel;
}


/**
 *
 * @return The real audiodevices sample rate
 */
uint TAudioDevice::get_sample_rate( ) const T_REALTIME_SAFE
{
    return m_rate;
}

/**
 *
 * @return The real bit depth, which is 32 bit float.... FIXME Need to get the real bitdepth as
 *		reported by the 'real audiodevice'
 */
uint TAudioDevice::get_bit_depth( ) const
{
    return m_bitdepth;
}

/**
 *
 * @return The short description of the 'real audio device'
 */
QString TAudioDevice::get_device_name( ) const
{
    if (m_driver)
        return m_driver->get_device_name();
    return tr("No Device Configured");
}

/**
 *
 * @return The long description of the 'real audio device'
 */
QString TAudioDevice::get_device_longname( ) const
{
    if (m_driver)
        return m_driver->get_device_longname();
    return tr("No Device Configured");
}

/**
 *
 * @return A list of supported Drivers
 */
QStringList TAudioDevice::get_available_drivers( ) const
{
    return m_availableDrivers;
}

/**
 *
 * @return The currently used Driver type
 */
QString TAudioDevice::get_driver_type( ) const
{
    return m_driverType;
}

QString TAudioDevice::get_driver_information() const
{
    if (m_driverType == "PortAudio") {
        QStringList list = m_setup.get_card_device().split("::");
        return "PortAudio: " + list.at(0);
    }

    return m_driverType;
}


/**
 *
 * @return The cpu load, call this at least 1 time per second to keep data consistent
 */
float TAudioDevice::get_cpu_time( )
{
    trav_time_t currentTime = TTimeRef::get_nanoseconds_since_epoch();
    // Taken and zeroed in one step: what a cycle adds in between is not lost
    const trav_time_t totalTime = m_processCallBackCpuTime.exchange(0);

    audio_sample_t result = ( (double(totalTime)  / (currentTime - m_lastCpuReadTime) ) * 100 );

    m_lastCpuReadTime = currentTime;

    return result;
}

void TAudioDevice::add_client( TAudioDeviceClient * client )
{
    {
        TRtTransaction transaction;
        m_clients.add(client, transaction);
    }

    client->set_connected(true);
    emit audioDeviceClientAdded(client);
}

void TAudioDevice::remove_client( TAudioDeviceClient * client )
{
    PENTER;

    if (!m_clients.gui_items().contains(client)) {
        TLOG_W("device", "client {} was not in the clients list, failed to remove it", client->m_name);
        return;
    }

    // The removal has to reach the audio thread now: inside a transaction it
    // would wait for the outermost one to end, and the wait below would
    // return before the audio thread stopped using the client
    Q_ASSERT_X(!TRtTransaction::is_open(), "TAudioDevice::remove_client",
               "a client cannot be removed inside a TRtTransaction");

    {
        TRtTransaction transaction;
        m_clients.remove(client, transaction);
    }

    // The owner deletes the client after this (a closed project deletes
    // itself), so the audio thread must be done with it
    if (!wait_until_changes_applied(1000)) {
        PERROR(QString("AudioDevice: the audio thread did not apply the removal of client %1").arg(client->m_name));
        return;
    }

    client->set_connected(false);
    emit audioDeviceClientRemoved(client);
}

bool TAudioDevice::wait_until_changes_applied(int timeoutMilliseconds)
{
    // The audio thread applies posted changes at the start of a cycle, then
    // processes that cycle with the new state and counts it. A change posted
    // after a cycle started waits for the next one, so two counts later the
    // old state is certainly no longer in use.
    const quint64 start = m_cycleCount.load(std::memory_order_acquire);

    // Check once per audio period, the time between two cycles
    const unsigned long periodMicroseconds = (m_rate > 0) ? (1000000UL * m_bufferSize) / m_rate : 1000;

    QElapsedTimer timer;
    timer.start();

    while (m_cycleCount.load(std::memory_order_acquire) < start + 2) {
        if (timer.elapsed() > timeoutMilliseconds) {
            return false;
        }
        QThread::usleep(periodMicroseconds);
    }

    return true;
}


void TAudioDevice::audiothread_finished()
{
    if (m_runAudioThread.load() && m_xrunCount.load() <= 30) {
        // AudioThread stopped, but we didn't do it ourselves
        // so something certainly did go wrong when starting the beast
        // Start the Dummy Driver to avoid problems with TSMP
        PERROR("Alsa/Jack AudioThread stopped, but we didn't ask for it! Something apparently did go wrong :-(");
        set_parameters(fallback_setup());
    }
}

void TAudioDevice::xrun( )
{
    const uint count = m_xrunCount.fetch_add(1, std::memory_order_relaxed) + 1;
    TLOG_RT_W("audio", "xrun {}", count);
    tsmp().post_to_gui_thread(TAudioThreadMessageQueueEvent{
        .rtTask = nullptr,
        .guiCallback = [this]() { emit bufferUnderRun(); }
    });

    // A storm: the GUI's watchdog timer sees the flag within half a second
    // and switches to the Dummy driver (check_audio_thread_progress());
    // set_parameters() stops this driver there. A flag, not a post: a post
    // can be dropped on a full queue, and JACK calls this from its
    // notification thread, a second producer on the queue. Not
    // m_driver->stop() here: that is the thread the driver drives. A
    // blocking driver's loop ends now; a callback driver's cycles go on
    // until the GUI is there
    if (count > 30) {
        m_xrunStorm.store(true);
        m_runAudioThread.store(false);
    }
}

void TAudioDevice::check_audio_thread_progress()
{
    if (m_xrunStorm.exchange(false)) {
        emit xrunStormDetected();
        return;
    }

    if (!m_driver || !m_runAudioThread.load()) {
        // Stopped on purpose, e.g. after a buffer underrun storm
        m_watchdog.restart(m_cycleCount.load(std::memory_order_relaxed), m_watchdogClock.elapsed());
        return;
    }

    if (!m_watchdog.is_stalled(m_cycleCount.load(std::memory_order_relaxed), m_watchdogClock.elapsed())) {
        return;
    }

    m_watchdogTimer.stop();

    const QString driverType = m_driverType;

    if (driverType != "Dummy") {
        PERROR(QString("AudioDevice: the %1 audio thread stopped processing, switching to the Dummy driver").arg(driverType));
        set_parameters(fallback_setup());
        emit audioThreadStalled(driverType);
    } else {
        PERROR(QString("AudioDevice: the Dummy driver audio thread stopped processing too"));
        emit audioThreadDead();
    }
}

void TAudioDevice::check_jack_shutdown()
{
#if defined (JACK_SUPPORT)
    TJackDriver* jackdriver = qobject_cast<TJackDriver*>(m_driver);
    if (jackdriver) {
        if ( ! jackdriver->is_running()) {
            jackShutDownChecker.stop();
            TLOG_W("device", "JACK shut down");
            driver_setup_message("Jack", tr("The Jack server has been shutdown!"), CRITICAL);
            delete m_driver;
            m_driver = nullptr;
            set_parameters(fallback_setup());
        }
    }
#endif
}

void TAudioDevice::driver_setup_message(const QString &driver, const QString &message, int severity, trav_time_t creatonOn)
{
    TAudioDriverSetupMessage setupMessage;
    setupMessage.message = message;
    setupMessage.driverType = driver;
    setupMessage.severity = severity;
    setupMessage.createdOn = creatonOn;
    m_audioDriverSetupMessages.insert(setupMessage.createdOn, setupMessage);

    // The log keeps what the driver messages widget shows, e.g. why a
    // driver fell back to the Dummy driver
    switch (severity) {
    case CRITICAL:
    case DRIVER_SETUP_FAILURE:
        TLOG_E("driver", "{}: {}", driver, message);
        break;
    case WARNING:
    case DRIVER_SETUP_WARNING:
        TLOG_W("driver", "{}: {}", driver, message);
        break;
    default:
        TLOG_I("driver", "{}: {}", driver, message);
        break;
    }
    emit newDriverSetupMessage();
}


// The Dummy driver runs at any rate and buffer size, so the fallback keeps
// the current ones: the clients' buffers stay valid and the sheets keep
// their rate
TAudioDeviceSetup TAudioDevice::fallback_setup() const
{
    TAudioDeviceSetup setup = m_fallBackSetup;
    if (m_rate > 0) {
        setup.set_sample_rate(m_rate);
    }
    if (m_bufferSize > 0) {
        setup.set_buffer_size(m_bufferSize);
    }
    return setup;
}

void TAudioDevice::switch_to_null_driver()
{
    if (m_driverType == "Dummy") {
        return;
    }
    driver_setup_message("AudioDevice", tr("Buffer underrun 'Storm' detected, switching to Dummy Driver"), CRITICAL);
    driver_setup_message("AudioDevice", tr("For trouble shooting this problem, please see Chapter 11 from the user manual!"), CRITICAL);
    set_parameters(fallback_setup());
}

int TAudioDevice::transport_control(TTransportControl *state)
{
#if defined (JACK_SUPPORT)
    if (!slaved_jack_driver()) {
        return true;
    }
#endif	

    int result = 0;

    // Called in the audio thread when the transport follows jack
    for (TAudioDeviceClient* client : m_clients.rt_items()) {
        result = client->transport_control(state);
    }

    return result;
}

void TAudioDevice::transport_start(TAudioDeviceClient * client)
{
#if defined (JACK_SUPPORT)
    TJackDriver* jackdriver = slaved_jack_driver();
    if (jackdriver) {
        PMESG("using jack_transport_start");
        jack_transport_start(jackdriver->get_client());
        return;
    }
#endif

    m_transportControl.set_state(TTransportControl::TransportRolling);
    m_transportControl.set_slave(false);
    m_transportControl.set_realtime(false);
    m_transportControl.set_location(TTimeRef()); // get from client!!

    client->transport_control(&m_transportControl);
}

void TAudioDevice::transport_stop(TAudioDeviceClient * client, const TTimeRef &location)
{
#if defined (JACK_SUPPORT)
    TJackDriver* jackdriver = slaved_jack_driver();
    if (jackdriver) {
        PMESG("using jack_transport_stop");
        jack_transport_stop(jackdriver->get_client());
        return;
    }
#endif

    m_transportControl.set_state(TTransportControl::TransportStopped);
    m_transportControl.set_slave(false);
    m_transportControl.set_realtime(false);
    m_transportControl.set_location(location);

    client->transport_control(&m_transportControl);
}

// return 0 if valid request, non-zero otherwise.
int TAudioDevice::transport_locate(TAudioDeviceClient* client, const TTimeRef& location)
{
#if defined (JACK_SUPPORT)
    TJackDriver* jackdriver = slaved_jack_driver();
    if (jackdriver) {
        PMESG("using jack_transport_locate");
        nframes_t frames = TTimeRef::to_nframes(TTimeRef::frame_nearest(location, get_sample_rate()));
        return jack_transport_locate(jackdriver->get_client(), frames);
    }
#endif

    m_transportControl.set_state(TTransportControl::TransportStarting);
    m_transportControl.set_slave(false);
    m_transportControl.set_realtime(false);
    m_transportControl.set_location(location);

    client->transport_control(&m_transportControl);

    return 0;
}

#if defined (JACK_SUPPORT)
TJackDriver* TAudioDevice::slaved_jack_driver()
{
    TJackDriver* jackdriver = qobject_cast<TJackDriver*>(m_driver);
    if (jackdriver && jackdriver->is_slave()) {
        return jackdriver;
    }

    return nullptr;
}
#endif

TTimeRef TAudioDevice::get_playback_latency() const
{
    if (m_rate == 0) {
        return TTimeRef();
    }

    const nframes_t frames = m_driver ? m_driver->get_playback_latency_frames() : m_bufferSize;
    return TTimeRef(frames, m_rate);
}

nframes_t TAudioDevice::get_capture_latency_frames() const
{
    return m_driver ? m_driver->get_capture_latency_frames() : m_bufferSize;
}

nframes_t TAudioDevice::get_playback_latency_frames() const
{
    return m_driver ? m_driver->get_playback_latency_frames() : m_bufferSize;
}

QString TAudioDevice::get_driver_latency_in_ms()
{
    if(!m_driver || m_rate == 0) {
        return QString("xx ms");
    }

    // The round trip: from an input through Traverso to an output
    const nframes_t frames = m_driver->get_capture_latency_frames() + m_driver->get_playback_latency_frames();
    return QString::number(double(frames) * 1000.0 / m_rate, 'f', 2).append(" ms");
}

void TAudioDevice::set_driver_properties(QHash< QString, QVariant > & properties)
{
    m_driverProperties = properties;
#if defined (JACK_SUPPORT)
    TJackDriver* jackdriver = qobject_cast<TJackDriver*>(m_driver);
    if (jackdriver) {
        jackdriver->update_config();
    }
#endif
}

QVariant TAudioDevice::get_driver_property(const QString& property, const QVariant& defaultValue)
{
    return m_driverProperties.value(property, defaultValue);
}

