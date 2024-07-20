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

$Id: AudioDevice.cpp,v 1.57 2009/11/16 19:50:43 n_doebelin Exp $
*/

#include "AudioDevice.h"
#include "AudioDeviceThread.h"
#include "Tsar.h"

#if defined (ALSA_SUPPORT)
#include "AlsaDriver.h"
#endif

#if defined (JACK_SUPPORT)
RELAYTOOL_JACK
#include "JackDriver.h"
#endif

#if defined (PORTAUDIO_SUPPORT)
#include "PADriver.h"
#endif

#if defined (PULSEAUDIO_SUPPORT)
#include "TPulseAudioDriver.h"
#endif

#if defined (COREAUDIO_SUPPORT)
#include "CoreAudioDriver.h"
#endif


#include "TAudioDriver.h"
#include "TAudioDeviceClient.h"
#include "AudioChannel.h"
#include "Tsar.h"

//#include <sys/mman.h>
#include <QDebug>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

/*! 	\class AudioDevice
    \brief An Interface to the 'real' audio device, and the hearth of the libtraversoaudiobackend

    AudioDevice is accessed by the audiodevice() function. You need to first initialize the 'device' by
    calling AudioDevice::set_parameters(int rate, nframes_t bufferSize, QString driverType);
    This will initialize the real audiodevice in case of the Alsa driver, or connect to the jack deamon.
    In the latter case, the rate and bufferSize don't do anything, since they are provided by the jack itself

        This class and/or related classes depend on RingBuffer, Tsar and FastDelegate which are found in 'src/common' directory.
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
        m_client->set_process_callback( MakeDelegate(this, &MyApp::process) );
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

/**
 * A global function, used to get the AudioDevice instance. Due the nature of singletons, the
   AudioDevice intance will be created automatically!
 * @return The AudioDevice instance, it will be automatically created on first call
 */
AudioDevice& audiodevice()
{ 
    static AudioDevice device;
    return device;
}

TsarEvent finishedOneProcessCycleEvent;

AudioDevice::AudioDevice()
{
    m_transportControl = new TTransportControl();
    m_runAudioThread = false;
    m_driver = nullptr;
    m_audioThread = nullptr;
    m_bufferSize = 1024;
    m_rate = 0;
    m_bitdepth = 0;
    m_xrunCount = 0;
    m_cpuTime = new RingBufferNPT<trav_time_t>(4096);
    m_cycleStartTime = {};
    m_lastCpuReadTime = {};

    m_driverType = tr("No Driver Loaded");

    m_fallBackSetup.driverType = "Null Driver";

#if defined (JACK_SUPPORT)
    if (libjack_is_present) {
        m_availableDrivers << "Jack";
    }
#endif

#if defined (ALSA_SUPPORT)
    m_availableDrivers << "ALSA";
#endif

#if defined (PORTAUDIO_SUPPORT)
    m_availableDrivers << "PortAudio";
#endif

#if defined (PULSEAUDIO_SUPPORT)
    m_availableDrivers << "PulseAudio";
#endif

#if defined (COREAUDIO_SUPPORT)
    m_availableDrivers << "CoreAudio";
#endif


    m_availableDrivers << "Null Driver";

    // This will create the event queueu and tsar thread for us
    // has to be running before the audio thread in order to make
    // sure no events will get lost
    tsar();

    connect(this, SIGNAL(xrunStormDetected()), this, SLOT(switch_to_null_driver()));
    connect(&m_xrunResetTimer, SIGNAL(timeout()), this, SLOT(reset_xrun_counter()));

    m_xrunResetTimer.start(30000);


    tsar().prepare_event(finishedOneProcessCycleEvent, this, nullptr, "", "finishedOneProcessCycle()");
}

AudioDevice::~AudioDevice()
{
    PENTERDES;

    shutdown();

    delete m_audioThread;
    delete m_cpuTime;
}

/**
 *
 * Not yet implemented
 */
void AudioDevice::show_descriptors( )
{
    // Needs to be implemented
}

void AudioDevice::set_buffer_size( nframes_t size )
{
    Q_ASSERT(size > 0);
    m_bufferSize = size;

    for (auto chan : m_channels) {
        chan->set_buffer_size(m_bufferSize);
    }

}

void AudioDevice::set_sample_rate( uint rate )
{
    m_rate = rate;
}

void AudioDevice::set_bit_depth( uint depth )
{
    m_bitdepth = depth;
}

int AudioDevice::run_cycle( nframes_t nframes, float delayed_usecs )
{
    nframes_t left;

    if (nframes != m_bufferSize) {
        printf ("late driver wakeup: nframes to process = %d\n", nframes);
    }

    /* run as many cycles as it takes to consume nframes (Should be 1 cycle!!)*/
    for (left = nframes; left >= m_bufferSize; left -= m_bufferSize) {
        if (run_one_cycle (m_bufferSize, delayed_usecs) < 0) {
            qCritical ("cycle execution failure, exiting");
            return -1;
        }
    }

    tsar().process_rt_event_slots();
    tsar().post_rt_event(finishedOneProcessCycleEvent);

    return 1;
}

int AudioDevice::run_one_cycle( nframes_t nframes, float  )
{

    if (m_driver->read(nframes) < 0) {
        qDebug("driver read failed!");
        return -1;
    }

    for(TAudioDeviceClient* client = m_clients.first(); client != nullptr; client = client->next) {
        client->process(nframes);
    }

    if (m_driver->write(nframes) < 0) {
        qDebug("driver write failed!");
        return -1;
    }


    return 0;
}

void AudioDevice::delay( float  )
{
}


/**
 * This function is used to initialize the AudioDevice's audioThread with the supplied
 * rate, bufferSize, channel/bus config, and driver type. In case the AudioDevice allready was configured,
 * it will stop the AudioDeviceThread and emits the stopped() signal,
 * re-inits the AlsaDriver with the new paramaters, when succesfull emits the driverParamsChanged() signal,
 * restarts the AudioDeviceThread and emits the started() signal
 *
 * @param TAudioDeviceSetup Contains all parameters the AudioDevice needs
 */
void AudioDevice::set_parameters(TAudioDeviceSetup ads)
{
    PENTER;

    m_rate = ads.rate;
    m_bufferSize = ads.bufferSize;
    m_xrunCount = 0;
    m_ditherShape = ads.ditherShape;
    //        if (!(ads.driverType == "Null Driver")) {
    m_setup = ads;
    //        }

    shutdown();

    if (create_driver(ads.driverType, ads.capture, ads.playback, ads.cardDevice) < 0) {
        set_parameters(m_fallBackSetup);
        return;
    }

    m_driver->attach();


    emit driverParamsChanged();

    m_runAudioThread = 1;

    if ((ads.driverType == "ALSA") || (ads.driverType == "Null Driver") || (ads.driverType == "PulseAudio") ) {

        printf("AudioDevice: Starting Audio Thread ... ");


        bool realTime = false;
        if (!m_audioThread) {
            if ((ads.driverType == "ALSA") || (ads.driverType == "Null Driver")) {
                realTime = true;
            }

            m_audioThread = new AudioDeviceThread(this, realTime);
        }

        // m_cycleStartTime/EndTime are set before/after the first cycle.
        // to avoid a "100%" cpu usage value during audioThread startup, set the
        // m_cycleStartTime here!
        m_cycleStartTime = TTimeRef::get_nanoseconds_since_epoch();

        // When the audiothread fails for some reason we catch it in audiothread_finished()
        // by connecting the finished signal of the audio thread!
        connect(m_audioThread, SIGNAL(finished()), this, SLOT(audiothread_finished()));

        // Start the audio thread, the driver->start() will be called from there!!
        m_audioThread->start();

        // It appears this check is a little silly because it always returns true
        // this close after calling the QThread::start() function :-(
        if (m_audioThread->isRunning()) {
            printf("Running!\n");
        }
    }

#if defined (JACK_SUPPORT)
    // This will activate the jack client
    if (libjack_is_present) {
        if (ads.driverType == "Jack") {

            if (m_driver->start() == -1) {
                // jack driver failed to start, fallback to Null Driver:
                set_parameters(m_fallBackSetup);
                return;
            }

            connect(&jackShutDownChecker, SIGNAL(timeout()), this, SLOT(check_jack_shutdown()));
            jackShutDownChecker.start(500);
        }
    }
#endif

    if (ads.driverType == "PortAudio"|| (ads.driverType == "PulseAudio") || (ads.driverType == "CoreAudio")) {
        if (m_driver->start() == -1) {
            // PortAudio driver failed to start, fallback to Null Driver:
            set_parameters(m_fallBackSetup);
            return;
        }
    }

    emit started();
}

int AudioDevice::create_driver(const QString& driverType, bool capture, bool playback, const QString& cardDevice)
{
    Q_ASSERT(!m_driver);

#if defined (JACK_SUPPORT)
    if (libjack_is_present) {
        if (driverType == "Jack") {
            m_driver = new JackDriver(this);
            JackDriver* jackDriver = qobject_cast<JackDriver*>(m_driver);
            if (jackDriver && jackDriver->setup(m_setup.jackChannels) < 0) {
                message(tr("Audiodevice: Failed to create the Jack Driver"), WARNING);
                delete m_driver;
                m_driver = nullptr;
                return -1;
            }
            m_driverType = driverType;
            return 1;
        }
    }
#endif

#if defined (ALSA_SUPPORT)
    if (driverType == "ALSA") {
        m_driver =  new AlsaDriver(this);
        AlsaDriver* alsaDriver = qobject_cast<AlsaDriver*>(m_driver);
        if (alsaDriver && alsaDriver->setup(capture,playback, cardDevice, m_ditherShape) < 0) {
            message(tr("Audiodevice: Failed to create the ALSA Driver"), WARNING);
            delete m_driver;
            m_driver = nullptr;
            return -1;
        }
        m_driverType = driverType;
        return 1;
    }
#endif

#if defined (PORTAUDIO_SUPPORT)
    if (driverType == "PortAudio") {
        m_driver = new PADriver(this);
        PADriver* paDriver = qobject_cast<PADriver*>(m_driver);
        if (paDriver && paDriver->setup(capture, playback, cardDevice) < 0) {
            message(tr("Audiodevice: Failed to create the PortAudio Driver"), WARNING);
            delete m_driver;
            m_driver = nullptr;
            return -1;
        }
        m_driverType = driverType;
        return 1;
    }
#endif

#if defined (PULSEAUDIO_SUPPORT)
    if (driverType == "PulseAudio") {
        m_driver = new TPulseAudioDriver(this);
        TPulseAudioDriver* paDriver = qobject_cast<TPulseAudioDriver*>(m_driver);
        if (paDriver && paDriver->setup(capture, playback, cardDevice) < 0) {
            message(tr("Audiodevice: Failed to create the PulseAudio Driver"), WARNING);
            delete m_driver;
            m_driver = nullptr;
            return -1;
        }
        m_driverType = driverType;
        return 1;
    }
#endif


#if defined (COREAUDIO_SUPPORT)
    if (driverType == "CoreAudio") {
        m_driver = new CoreAudioDriver(this, m_rate, m_bufferSize);
        CoreAudioDriver* coreAudioDriver = qojbect_cast<CoreAudioDriver*>(m_driver);
        if (coreAudioDriver && coreAudiodriver->setup(capture, playback, cardDevice) < 0) {
            message(tr("Audiodevice: Failed to create the CoreAudio Driver"), WARNING);
            delete m_driver;
            m_driver = nullptr;
            return -1;
        }
        m_driverType = driverType;
        return 1;
    }
#endif


    if (driverType == "Null Driver") {
        printf("AudioDevice: Creating Null Driver...\n");
        m_driver = new TAudioDriver(this);
        m_driverType = driverType;
        return 1;
    }

    return -1;
}


/**
 * Stops the AudioDevice's AudioThread, free's any related memory.
 
 * Use this to properly shut down the AudioDevice on application exit,
 * or to explicitely release the real 'audiodevice'.
 
 * Use set_parameters() to reinitialize the audiodevice if you want to use it again.
 *
 * @return 1 on succes, 0 on failure
 */
int AudioDevice::shutdown( )
{
    PENTER;
    int r = 1;

    emit stopped();

    m_runAudioThread = 0;

    if (m_audioThread) {
        disconnect(m_audioThread, SIGNAL(finished()), this, SLOT(audiothread_finished()));

        // Wait until the audioThread has finished execution. One second
        // should do, if it's still running then, the thread must have gone wild or something....
        if (m_audioThread->isRunning()) {
            printf("AudioDevice: Starting to shutdown Audio Thread ... \n");
            r = m_audioThread->wait(1000);
            printf("AudioDevice: Audio Thread finished, stopping driver\n");
        }

        delete m_audioThread;
        m_audioThread = nullptr;
    }


    if (m_driver) {
        m_driver->stop();

        QList<AudioChannel*> channels = m_driver->get_capture_channels();
        channels.append(m_driver->get_playback_channels());
        foreach(AudioChannel* chan, channels) {
            m_channels.removeAll(chan);
        }

        delete m_driver;
        m_driver = nullptr;
    }

    return r;
}

QStringList AudioDevice::get_capture_channel_names() const
{
    QStringList names;
    foreach(AudioChannel* chan, m_driver->get_capture_channels()) {
        names.append(chan->get_name());
    }
    return names;
}

QStringList AudioDevice::get_playback_channel_names() const
{
    QStringList names;
    foreach(AudioChannel* chan, m_driver->get_playback_channels()) {
        names.append(chan->get_name());
    }
    return names;
}

QList<AudioChannel*> AudioDevice::get_channels() const
{
    QList<AudioChannel*> channels;
    if (!m_driver) {
        return channels;
    }

    channels.append(m_driver->get_capture_channels());
    channels.append(m_driver->get_playback_channels());

    return channels;
}

QList<AudioChannel*> AudioDevice::get_capture_channels() const
{
    QList<AudioChannel*> channels;
    if (!m_driver) {
        return channels;
    }

    return m_driver->get_capture_channels();
}

QList<AudioChannel*> AudioDevice::get_playback_channels() const
{
    QList<AudioChannel*> channels;
    if (!m_driver) {
        return channels;
    }

    return m_driver->get_playback_channels();
}

int AudioDevice::add_jack_channel(AudioChannel *channel)
{
#if defined (JACK_SUPPORT)
    if (libjack_is_present) {
        JackDriver* jackdriver = qobject_cast<JackDriver*>(m_driver);
        if (!jackdriver) {
            return -1;
        }

        jackdriver->add_channel(channel);

        return 1;
    }
#endif

    return -1;
}

void AudioDevice::remove_jack_channel(AudioChannel *channel)
{
#if defined (JACK_SUPPORT)
    if (libjack_is_present) {
        JackDriver* jackdriver = qobject_cast<JackDriver*>(m_driver);
        if (!jackdriver) {
            return;
        }

        printf("removing channel from jackdriver\n");
        jackdriver->remove_channel(channel);
    }
#endif
}

AudioChannel* AudioDevice::get_capture_channel_by_name(const QString &name)
{
    if (!m_driver) {
        return nullptr;
    }
    return m_driver->get_capture_channel_by_name(name);
}


AudioChannel* AudioDevice::get_playback_channel_by_name(const QString &name)
{
    if (!m_driver) {
        return nullptr;
    }
    return m_driver->get_playback_channel_by_name(name);
}

AudioChannel* AudioDevice::create_channel(const QString& name, uint channelNumber, int type)
{
    AudioChannel* chan = new AudioChannel(name, channelNumber, type);
    chan->set_buffer_size(m_bufferSize);
    m_channels.append(chan);
    return chan;
}

void AudioDevice::delete_channel(AudioChannel* channel)
{
    m_channels.removeAll(channel);
    delete channel;
}


/**
 *
 * @return The real audiodevices sample rate
 */
uint AudioDevice::get_sample_rate( ) const
{
    return m_rate;
}

/**
 *
 * @return The real bit depth, which is 32 bit float.... FIXME Need to get the real bitdepth as
 *		reported by the 'real audiodevice'
 */
uint AudioDevice::get_bit_depth( ) const
{
    return m_bitdepth;
}

/**
 *
 * @return The short description of the 'real audio device'
 */
QString AudioDevice::get_device_name( ) const
{
    if (m_driver)
        return m_driver->get_device_name();
    return tr("No Device Configured");
}

/**
 *
 * @return The long description of the 'real audio device'
 */
QString AudioDevice::get_device_longname( ) const
{
    if (m_driver)
        return m_driver->get_device_longname();
    return tr("No Device Configured");
}

/**
 *
 * @return A list of supported Drivers
 */
QStringList AudioDevice::get_available_drivers( ) const
{
    return m_availableDrivers;
}

/**
 *
 * @return The currently used Driver type
 */
QString AudioDevice::get_driver_type( ) const
{
    return m_driverType;
}

QString AudioDevice::get_driver_information() const
{
    if (m_driverType == "PortAudio") {
        QStringList list = m_setup.cardDevice.split("::");
        return "PA: " + list.at(0);
    }
    return m_driverType;
}


/**
 *
 * @return The cpu load, call this at least 1 time per second to keep data consistent
 */
float AudioDevice::get_cpu_time( )
{
#if defined (JACK_SUPPORT)
    if (libjack_is_present)
        if (m_driver && m_driverType == "Jack")
            return qobject_cast<JackDriver*>(m_driver)->get_cpu_load();
#endif

#if defined (PORTAUDIO_SUPPORT)
    if (m_driver && m_driverType == "PortAudio")
        return ((PADriver*)m_driver)->get_cpu_load();
#endif


    trav_time_t currentTime = TTimeRef::get_nanoseconds_since_epoch();
    float totaltime = 0;
    trav_time_t value = 0;
    int read = m_cpuTime->read_space();

    while (read != 0) {
        read = m_cpuTime->read(&value, 1);
        totaltime += value;
    }

    audio_sample_t result = ( (totaltime  / (currentTime - m_lastCpuReadTime) ) * 100 );

    m_lastCpuReadTime = currentTime;

    return result;
}

void AudioDevice::private_add_client(TAudioDeviceClient* client)
{
    m_clients.prepend(client);
}

void AudioDevice::private_remove_client(TAudioDeviceClient* client)
{
    PENTER;
    if (!m_clients.remove(client)) {
        printf("AudioDevice:: Client was not in clients list, failed to remove it!\n");
    }
}

/**
 * Adds the client into the audio processing chain in a Thread Save way

 * WARNING: This function assumes the Clients callback function is set to an existing objects function!
 */
void AudioDevice::add_client( TAudioDeviceClient * client )
{
    tsar().add_gui_event(this, client, "private_add_client(TAudioDeviceClient*)", "audioDeviceClientAdded(TAudioDeviceClient*)");
}

/**
 * Removes the client into the audio processing chain in a Thread save way
 *
 * The clientRemoved(Client* client); signal will be emited after succesfull removal
 * from within the GUI Thread!
 */
void AudioDevice::remove_client( TAudioDeviceClient * client )
{
    tsar().add_gui_event(this, client, "private_remove_client(TAudioDeviceClient*)", "audioDeviceClientRemoved(TAudioDeviceClient*)");
}

void AudioDevice::audiothread_finished() 
{
    if (m_runAudioThread) {
        // AudioThread stopped, but we didn't do it ourselves
        // so something certainly did go wrong when starting the beast
        // Start the Null Driver to avoid problems with Tsar
        PERROR("Alsa/Jack AudioThread stopped, but we didn't ask for it! Something apparently did go wrong :-(");
        set_parameters(m_fallBackSetup);
    }
}

void AudioDevice::xrun( )
{
    tsar().add_rt_event(this, nullptr, "bufferUnderRun()");

    m_xrunCount++;
    if (m_xrunCount > 30) {
        tsar().add_rt_event(this, nullptr, "xrunStormDetected()");
    }
}

void AudioDevice::check_jack_shutdown()
{
#if defined (JACK_SUPPORT)
    if (libjack_is_present) {
        JackDriver* jackdriver = qobject_cast<JackDriver*>(m_driver);
        if (jackdriver) {
            if ( ! jackdriver->is_running()) {
                jackShutDownChecker.stop();
                printf("jack shutdown detected\n");
                message(tr("The Jack server has been shutdown!"), CRITICAL);
                delete m_driver;
                m_driver = nullptr;
                set_parameters(m_fallBackSetup);
            }
        }
    }
#endif
}


void AudioDevice::switch_to_null_driver()
{
    message(tr("AudioDevice:: Buffer underrun 'Storm' detected, switching to Null Driver"), CRITICAL);
    message(tr("AudioDevice:: For trouble shooting this problem, please see Chapter 11 from the user manual!"), CRITICAL);
    set_parameters(m_fallBackSetup);
}

int AudioDevice::transport_control(TTransportControl *state)
{
#if defined (JACK_SUPPORT)
    if (!slaved_jack_driver()) {
        return true;
    }
#endif	

    int result = 0;

    for(TAudioDeviceClient* client = m_clients.first(); client != nullptr; client = client->next) {
        result = client->transport_control(state);
    }

    return result;
}

void AudioDevice::transport_start(TAudioDeviceClient * client)
{
#if defined (JACK_SUPPORT)
    JackDriver* jackdriver = slaved_jack_driver();
    if (jackdriver) {
        PMESG("using jack_transport_start");
        jack_transport_start(jackdriver->get_client());
        return;
    }
#endif

    m_transportControl->set_state(TTransportControl::Rolling);
    m_transportControl->set_slave(false);
    m_transportControl->set_realtime(false);
    m_transportControl->set_location(TTimeRef()); // get from client!!

    client->transport_control(m_transportControl);
}

void AudioDevice::transport_stop(TAudioDeviceClient * client, const TTimeRef &location)
{
#if defined (JACK_SUPPORT)
    JackDriver* jackdriver = slaved_jack_driver();
    if (jackdriver) {
        PMESG("using jack_transport_stop");
        jack_transport_stop(jackdriver->get_client());
        return;
    }
#endif

    m_transportControl->set_state(TTransportControl::Stopped);
    m_transportControl->set_slave(false);
    m_transportControl->set_realtime(false);
    m_transportControl->set_location(location);

    client->transport_control(m_transportControl);
}

// return 0 if valid request, non-zero otherwise.
int AudioDevice::transport_seek_to(TAudioDeviceClient* client, const TTimeRef& location)
{
#if defined (JACK_SUPPORT)
    JackDriver* jackdriver = slaved_jack_driver();
    if (jackdriver) {
        PMESG("using jack_transport_locate");
        nframes_t frames = TTimeRef::to_frame(location, get_sample_rate());
        return jack_transport_locate(jackdriver->get_client(), frames);
    }
#endif

    m_transportControl->set_state(TTransportControl::Starting);
    m_transportControl->set_slave(false);
    m_transportControl->set_realtime(false);
    m_transportControl->set_location(location);

    client->transport_control(m_transportControl);

    return 0;
}

#if defined (JACK_SUPPORT)
JackDriver* AudioDevice::slaved_jack_driver()
{
    if (libjack_is_present) {
        JackDriver* jackdriver = qobject_cast<JackDriver*>(m_driver);
        if (jackdriver && jackdriver->is_slave()) {
            return jackdriver;
        }
    }

    return nullptr;
}
#endif

TTimeRef AudioDevice::get_buffer_latency()
{
    return {m_bufferSize, m_rate};
}

void AudioDevice::set_driver_properties(QHash< QString, QVariant > & properties)
{
    m_driverProperties = properties;
#if defined (JACK_SUPPORT)
    if (libjack_is_present) {
        JackDriver* jackdriver = qobject_cast<JackDriver*>(m_driver);
        if (jackdriver) {
            jackdriver->update_config();
        }
    }
#endif
}

QVariant AudioDevice::get_driver_property(const QString& property, const QVariant& defaultValue)
{
    return m_driverProperties.value(property, defaultValue);
}

