/*
    Copyright (C) 2007-2010 Remon Sijrier
 
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

#include "TPortAudioDriver.h"

#include "TAudioDevice.h"
#include "AudioChannel.h"

#include <Utils.h>

#include "Debugger.h"


// TODO Is there an xrun callback for PortAudio? If so, connect to _xrun_callback
// TODO If there is some portaudio shutdown callback, connect to _on_pa_shutdown_callback
//	and make it work!

TPortAudioDriver::TPortAudioDriver( TAudioDevice * device)
    : TAudioDriver(device)
    , m_paInputBuffer(device->get_buffer_size())
    , m_paOutputBuffer(device->get_buffer_size())
{
    read = TAudioDriverReadWriteCallBack::from_method<TPortAudioDriver, &TPortAudioDriver::_read>(this);
    write = TAudioDriverReadWriteCallBack::from_method<TPortAudioDriver, &TPortAudioDriver::_write>(this);
    run_cycle = TRunCycleCallBack::from_method<TPortAudioDriver, &TPortAudioDriver::_run_cycle>(this);

    m_paStream = nullptr;
}

TPortAudioDriver::~TPortAudioDriver( )
{
    PENTER;

    Pa_CloseStream( m_paStream );
    Pa_Terminate();
}

// PortAudio reports the output latency of the open stream in seconds; a
// host API that reports none gets the base class's estimate, as JACK and
// PipeWire do. Not verified at runtime: no PortAudio device was available
nframes_t TPortAudioDriver::get_playback_latency_frames() const
{
    if (m_paStream) {
        if (const PaStreamInfo* info = Pa_GetStreamInfo(m_paStream)) {
            const nframes_t frames = nframes_t(info->outputLatency * m_frameRate + 0.5);
            if (frames > 0) {
                return frames;
            }
        }
    }
    return TAudioDriver::get_playback_latency_frames();
}

// The input latency of the open stream, the same way (not verified at
// runtime either)
nframes_t TPortAudioDriver::get_capture_latency_frames() const
{
    if (m_paStream) {
        if (const PaStreamInfo* info = Pa_GetStreamInfo(m_paStream)) {
            const nframes_t frames = nframes_t(info->inputLatency * m_frameRate + 0.5);
            if (frames > 0) {
                return frames;
            }
        }
    }
    return TAudioDriver::get_capture_latency_frames();
}

int TPortAudioDriver::_read(nframes_t nframes) T_REALTIME_SAFE
{
    Q_ASSERT(m_captureChannels.size() > 0);
    Q_ASSERT(m_paStream);

    // The clock: the blocking API waits here for the device
    // (doc/design/realtime-audio-path.md §5)
    T_BLOCKING_BY_DESIGN(Pa_ReadStream(m_paStream, m_paInputBuffer.get_data(nframes), nframes);)

    const trav_time_t now = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_start_time(now);

    uint channelCount = m_captureChannels.size();

    for (uint chan = 0; chan < channelCount; chan++) {
        AudioChannel* channel = m_captureChannels.at(chan);
        TChannelBuffer &buf = channel->get_buffer();
        for(nframes_t frame=0; frame<nframes; frame++) {
            buf[frame] = m_paInputBuffer[frame * channelCount + chan];
        }
    }

    return 1;
}

int TPortAudioDriver::_write(nframes_t nframes) T_REALTIME_SAFE
{
    Q_ASSERT(m_playbackChannels.size() > 0);
    Q_ASSERT(m_paStream);

    uint channelCount = m_playbackChannels.size();
    for (uint chan = 0; chan < channelCount; chan++) {
        TChannelBuffer &buffer = m_playbackChannels.at(chan)->get_buffer();
        for (nframes_t frame = 0; frame < nframes; frame++) {
            m_paOutputBuffer[frame * channelCount + chan] = buffer.at(frame);
        }
    }


    for (int chan=0; chan<m_playbackChannels.size(); chan++) {
        m_playbackChannels.at(chan)->silence_buffer();
    }

    const trav_time_t now = TTimeRef::get_nanoseconds_since_epoch();
    m_device->set_transport_cycle_end_time(now);

    // The clock: the blocking API waits here for the device
    T_BLOCKING_BY_DESIGN(Pa_WriteStream(m_paStream, m_paOutputBuffer.get_data(nframes), nframes);)

    return 1;
}

int TPortAudioDriver::_run_cycle()
{
    return m_device->run_cycle(m_framesPerCycle, 0);
}

QStringList TPortAudioDriver::devices_info(const QString& hostApi)
{
    QStringList list;
    PaError err = Pa_Initialize();

    if( err != paNoError ) {
        Pa_Terminate();
        return list;
    }

    PaDeviceIndex hostIndex = host_index_for_host_api(hostApi);

    if (hostIndex == paHostApiNotFound) {
        Pa_Terminate();
        return list;
    }

    const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(hostIndex);

    QString device;

    for (int i=0; i<hostApiInfo->deviceCount; ++i) {
        PaDeviceIndex paDeviceIndex= Pa_HostApiDeviceIndexToDeviceIndex(hostIndex, i);
        device = Pa_GetDeviceInfo(paDeviceIndex)->name;
        if (paDeviceIndex == hostApiInfo->defaultInputDevice) {
            device += "###defaultInputDevice";
        }
        if (paDeviceIndex == hostApiInfo->defaultOutputDevice) {
            device += "###defaultOutputDevice";
        }
        list.append(device);
    }

    Pa_Terminate();

    return list;
}

int TPortAudioDriver::host_index_for_host_api(const QString& hostapi)
{
    int hostIndex = paHostApiNotFound;

    if (hostapi == "alsa") {
        hostIndex = Pa_HostApiTypeIdToHostApiIndex(paALSA);
    }

    if (hostapi == "jack") {
        hostIndex = Pa_HostApiTypeIdToHostApiIndex(paJACK);
    }

    if (hostapi == "wmme") {
        hostIndex = Pa_HostApiTypeIdToHostApiIndex(paMME);
    }

    if (hostapi == "directsound") {
        hostIndex = Pa_HostApiTypeIdToHostApiIndex(paDirectSound);
    }

    if (hostapi == "wasapi") {
        hostIndex = Pa_HostApiTypeIdToHostApiIndex(paWASAPI);
    }

    if (hostapi == "wdmks") {
        hostIndex = Pa_HostApiTypeIdToHostApiIndex(paWDMKS);
    }

    if (hostapi == "asio") {
        hostIndex = Pa_HostApiTypeIdToHostApiIndex(paASIO);
    }

    if (hostapi == "coreaudio") {
        hostIndex = Pa_HostApiTypeIdToHostApiIndex(paCoreAudio);
    }

    if (hostIndex >= 0) {
        TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "PADriver:: Found %s host api", qUtf8Printable(hostapi));
    }

    return hostIndex;
}

int TPortAudioDriver::setup(bool capture, bool playback, const QString& deviceInfo)
{
    TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "PADriver::setup");
    // TODO In case of hostapi == "alsa", the callback thread prio needs to be set to realtime.
    // 	there has been some discussion on this on the pa mailinglist, digg it up!

    m_frameRate = m_device->get_sample_rate();
    m_framesPerCycle = m_device->get_buffer_size();

    QStringList deviceInfos = deviceInfo.split("::");
    QString hostapi, inputDeviceName, outputDeviceName;
    if (deviceInfos.size() > 0) {
        hostapi = deviceInfos.at(0);
    }
    if (deviceInfos.size() > 1 && capture) {
        inputDeviceName = deviceInfos.at(1);
    }
    if (deviceInfos.size() > 2 && playback) {
        outputDeviceName = deviceInfos.at(2);
    }

    emit driverSetupMessage("PortAudio", tr("Setting up PortAudio using %1").arg(Pa_GetVersionText()), TAudioDevice::DRIVER_SETUP_INFO);
    emit driverSetupMessage("PortAudio", tr("Driver: %1, capture: %2, playback: %3 <br />Input Device: %4 <br />Output Device: %5").
                            arg(hostapi).arg(capture ? tr("yes") : tr("no")).arg(playback ? tr("yes") : tr("no")).
                            arg(inputDeviceName).arg(outputDeviceName), TAudioDevice::DRIVER_SETUP_INFO);

    PaError err = Pa_Initialize();

    if( err != paNoError ) {
        emit driverSetupMessage("PortAudio", (tr("Failed to initialize PortAudio: %1").arg(Pa_GetErrorText( err ))), TAudioDevice::DRIVER_SETUP_FAILURE);
        Pa_Terminate();
        return -1;
    }

    PaStreamParameters outputParameters, inputParameters;
    memset(&inputParameters, 0, sizeof(inputParameters));
    memset(&outputParameters, 0, sizeof(outputParameters));

    PaHostApiIndex hostIndex = host_index_for_host_api(hostapi);

    if (hostIndex == paHostApiNotFound) {
        emit driverSetupMessage("PortAudio", tr("PADriver:: hostapi %1 was not found by Portaudio!").arg(hostapi), TAudioDevice::DRIVER_SETUP_FAILURE);
        Pa_Terminate();
        return -1;
    }

    const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(hostIndex);

    PaDeviceIndex inputDeviceIndex = hostApiInfo->defaultInputDevice;
    PaDeviceIndex outputDeviceIndex = hostApiInfo->defaultOutputDevice;

    for (int i=0; i<hostApiInfo->deviceCount; ++i) {
        if (Pa_GetDeviceInfo(i)->name == inputDeviceName) {
            inputDeviceIndex = i;
        }
        if (Pa_GetDeviceInfo(i)->name == outputDeviceName) {
            outputDeviceIndex = i;
        }
    }

    int inChannelMax = 0;
    int outChannelsMax = 0;

    if( outputDeviceIndex != paNoDevice) {
        outChannelsMax = Pa_GetDeviceInfo(outputDeviceIndex)->maxOutputChannels;
        outputParameters.device = outputDeviceIndex;
        outputParameters.channelCount = outChannelsMax;
        outputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
        outputParameters.suggestedLatency = double(m_framesPerCycle) / m_frameRate;// Pa_GetDeviceInfo( outputParameters.device )->defaultHighOutputLatency;
        outputParameters.hostApiSpecificStreamInfo = NULL;
    }

    if (inputDeviceIndex != paNoDevice) {
        inChannelMax = Pa_GetDeviceInfo(inputDeviceIndex)->maxInputChannels;
        inputParameters.device = inputDeviceIndex;
        inputParameters.channelCount = inChannelMax;
        inputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
        inputParameters.suggestedLatency = double(m_framesPerCycle) / m_frameRate; // Pa_GetDeviceInfo( inputParameters.device )->defaultHighInputLatency;
        inputParameters.hostApiSpecificStreamInfo = NULL;
    }


    /* Open an audio I/O stream. */
    err = Pa_OpenStream(
        &m_paStream,
        capture ? &inputParameters : 0,	// The input parameter
        playback ? &outputParameters : 0,	// The outputparameter
        m_frameRate,		// Set in the constructor
        m_framesPerCycle,	// Set in the constructor
        paNoFlag,		// Don't use any flags
        NULL, 	// our callback function
        this );

    if( err == paNoError ) {
        emit driverSetupMessage("PortAudio", tr("Succesfully connected to PortAudio: %1").arg(Pa_GetVersionText()), TAudioDevice::DRIVER_SETUP_SUCCESS);
    } else {
        emit driverSetupMessage("PortAudio", (tr("Failed to open PortAudio stream: %1").arg(Pa_GetErrorText( err ))), TAudioDevice::DRIVER_SETUP_FAILURE);
        Pa_Terminate();
        return -1;
    }

    AudioChannel* audiochannel;
    char buf[32];

    if (playback) {
        for (int chn = 0; chn < outChannelsMax; chn++) {

            snprintf (buf, sizeof(buf) - 1, "playback_%d", chn+1);

            audiochannel = TAudioDriver::add_playback_channel(buf);
            audiochannel->set_latency(m_framesPerCycle + m_captureFrameLatency);
        }
    }

    if (capture) {
        for (int chn = 0; chn < inChannelMax; chn++) {

            snprintf (buf, sizeof(buf) - 1, "capture_%d", chn+1);

            audiochannel = TAudioDriver::add_capture_channel(buf);
            audiochannel->set_latency(m_framesPerCycle + m_captureFrameLatency);
        }
    }

    return 1;
}

int TPortAudioDriver::attach()
{
    TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "PADriver::attach()");
    m_device->set_buffer_size (m_framesPerCycle);
    m_device->set_sample_rate (m_frameRate);

    m_paInputBuffer.resize(m_framesPerCycle * sizeof(audio_sample_t) * m_captureChannels.count());
    m_paOutputBuffer.resize(m_framesPerCycle * sizeof(audio_sample_t) * m_playbackChannels.count());

    return 1;
}

int TPortAudioDriver::start( )
{
    PENTER;

    TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "PADriver::start()");

    PaError err = Pa_StartStream( m_paStream );

    if( err == paNoError ) {
        emit driverSetupMessage("PortAudio", tr("Succesfully started PortAudio stream."), TAudioDevice::DRIVER_SETUP_SUCCESS);
    } else {
        emit driverSetupMessage("PortAudio", (tr("Failed to start PortAudio stream: %1").arg(Pa_GetErrorText( err ))), TAudioDevice::DRIVER_SETUP_FAILURE);
        return -1;
    }

    // silences the playback buffers
    TAudioDriver::start();

    return 1;
}

int TPortAudioDriver::stop( )
{
    PENTER;
    PaError err = Pa_StopStream(m_paStream);

    if( err == paNoError ) {
        emit driverSetupMessage("PortAudio", tr("PADriver:: Successfully stopped PortAudio stream."), TAudioDevice::DRIVER_SETUP_SUCCESS);
    } else {
        emit driverSetupMessage("PortAudio", (tr("PADriver:: Failed to close PortAudio stream: %1").arg(Pa_GetErrorText( err ))), TAudioDevice::WARNING);
    }

    // silence capture channels
    TAudioDriver::stop();

    return 1;
}

int TPortAudioDriver::process_callback (nframes_t nframes)
{
    if (m_device->run_cycle( nframes, 0.0) == -1) {
        return paAbort;
    }

    return paContinue;
}

QString TPortAudioDriver::get_device_name( )
{
    // TODO get it from portaudio ?
    return "AudioDevice";
}

QString TPortAudioDriver::get_device_longname( )
{
    // TODO get it from portaudio ?
    return "AudioDevice";
}

int TPortAudioDriver::_xrun_callback( void * arg )
{
    TPortAudioDriver* driver  = static_cast<TPortAudioDriver *> (arg);
    driver->m_device->xrun();
    return 0;
}

void TPortAudioDriver::_on_pa_shutdown_callback(void * arg)
{
    Q_UNUSED(arg);
}

int TPortAudioDriver::_process_callback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *arg )
{
    Q_UNUSED(timeInfo);
    Q_UNUSED(statusFlags);
    Q_UNUSED(inputBuffer);
    Q_UNUSED(outputBuffer);
    Q_UNUSED(framesPerBuffer);
    Q_UNUSED(arg);

    // TPortAudioDriver* driver  = static_cast<TPortAudioDriver *> (arg);

    // driver->m_paInputBuffer = (audio_sample_t*)inputBuffer;
    // driver->m_paOutputBuffer = (audio_sample_t*)outputBuffer;

    // driver->process_callback (framesPerBuffer);

    return 0;
}

float TPortAudioDriver::get_cpu_load( )
{
    return Pa_GetStreamCpuLoad(m_paStream) * 100;
}


//eof
