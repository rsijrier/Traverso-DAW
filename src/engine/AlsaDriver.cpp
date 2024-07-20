/*
Copyright (C) 2005-2010 Remon Sijrier

(December 2005) Ported to C++ for Traverso by Remon Sijrier
Copyright (C) 2001 Paul Davis 

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

/* ALSA driver based on jack-audio-connection-kit-0.109.0 alsa_driver.c */


#include "AlsaDriver.h"
#include "AudioChannel.h"
#include <Utils.h>

#include <cmath>
#include <cstdio>
#include <memory.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>
#include <sys/types.h>
#include <regex.h>

#include <cstring>
#include <sys/time.h>
#include <ctime>

#include "AudioDevice.h"

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

#undef DEBUG_WAKEUP

/* Delay (in process calls) before Traverso will report an xrun */
#define XRUN_REPORT_DELAY 0


AlsaDriver::AlsaDriver(AudioDevice* device)
    : TAudioDriver(device)
{
    read = MakeDelegate(this, &AlsaDriver::_read);
    write = MakeDelegate(this, &AlsaDriver::_write);
    run_cycle = RunCycleCallback(this, &AlsaDriver::_run_cycle);
}

AlsaDriver::~AlsaDriver()
{
    PENTERDES;
    if (capture_handle) {
        snd_pcm_close (capture_handle);
    }

    if (playback_handle) {
        snd_pcm_close (playback_handle);
    }

    if (capture_hw_params) {
        snd_pcm_hw_params_free (capture_hw_params);
    }

    if (playback_hw_params) {
        snd_pcm_hw_params_free (playback_hw_params);
    }

    if (capture_sw_params) {
        snd_pcm_sw_params_free (capture_sw_params);
    }

    if (playback_sw_params) {
        snd_pcm_sw_params_free (playback_sw_params);
    }

    if (pfd) {
        free (pfd);
    }

    free (alsa_name_playback);
    free (alsa_name_capture);

    release_channel_dependent_memory ();
}


int AlsaDriver::setup(bool capture, bool playback, const QString& devicename, const QString& ditherShape)
{
    m_framesPerCycle = m_device->get_buffer_size();
    m_frameRate = m_device->get_sample_rate();

    int user_nperiods = m_device->get_driver_property("numberofperiods", 3).toInt();

    QString pcmName = "default";
    if (devicename != "default") {
        pcmName = QString("hw:%1").arg(devicename);
        printf(" devicename %s\n", devicename.toLatin1().data());
    }

    char *playback_pcm_name = strdup(pcmName.toLatin1().data());
    char *capture_pcm_name = strdup(pcmName.toLatin1().data());
    int shorts_first = false;

    /* duplex is the default */
    if (!capture && !playback) {
        capture = true;
        playback = true;
    }


    int err;
    playback_handle = nullptr;
    capture_handle = nullptr;
    ctl_handle = nullptr;
    capture_and_playback_not_synced = false;
    capture_interleaved = false;
    playback_interleaved = false;
    max_nchannels = 0;
    user_nchannels = 0;
    playback_nchannels = 0;
    capture_nchannels = 0;
    playback_sample_bytes = (shorts_first ? 2:4);
    capture_sample_bytes = (shorts_first ? 2:4);
    m_captureFrameLatency = 0;
    m_playbackFrameLatency = 0;
    channels_done = nullptr;
    channels_not_done = nullptr;
    m_ditherState = nullptr;

    playback_addr = nullptr;
    capture_addr = nullptr;
    playback_interleave_skip = nullptr;
    capture_interleave_skip = nullptr;


    playback_hw_params = nullptr;
    capture_hw_params = nullptr;
    playback_sw_params = nullptr;
    capture_sw_params = nullptr;


    silent = nullptr;

    pfd = nullptr;
    playback_nfds = 0;
    capture_nfds = 0;

    if (ditherShape == "Rectangular") {
        m_dither = Rectangular;
    } else if (ditherShape == "Shaped") {
        m_dither = Shaped;
    } else if (ditherShape == "Triangular") {
        m_dither = Triangular;
    } else {
        m_dither = None;
    }

    soft_mode = false;

    quirk_bswap = 0;

    process_count = 0;

    alsa_name_playback = strdup (playback_pcm_name);
    alsa_name_capture = strdup (capture_pcm_name);

    if (playback) {
        if (snd_pcm_open (&playback_handle, alsa_name_playback, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK) < 0) {
            switch (errno) {
            case EBUSY:
                m_device->driverSetupMessage(tr("The playback device '%1'' is already in use. Please stop the"
                                              "application using it and run Traverso again").
                                           arg(playback_pcm_name), AudioDevice::DRIVER_SETUP_FAILURE);
                return -1;

            case EPERM:
                m_device->driverSetupMessage(tr("You do not have permission to open the audio device '%1' for playback").
                                           arg(playback_pcm_name), AudioDevice::DRIVER_SETUP_FAILURE);
                return -1;
            default:
                m_device->driverSetupMessage(tr("Opening Playback Device '%1' failed with unknown error type").
                                           arg(playback_pcm_name), AudioDevice::DRIVER_SETUP_WARNING);
            }

            playback_handle = nullptr;
        }

        if (playback_handle) {
            snd_pcm_nonblock (playback_handle, 0);
        }
    }

    if (capture) {
        if (snd_pcm_open (&capture_handle, alsa_name_capture, SND_PCM_STREAM_CAPTURE,  SND_PCM_NONBLOCK) < 0) {
            switch (errno) {
            case EBUSY:
                m_device->driverSetupMessage(tr("The Capture Device %1 is already in use. Please stop the"
                                              " application using it and run Traverso again").arg(capture_pcm_name), AudioDevice::DRIVER_SETUP_FAILURE);
                return -1;

            case EPERM:
                m_device->driverSetupMessage(tr("You do not have permission to open Device %1 for capture").
                                           arg(capture_pcm_name), AudioDevice::DRIVER_SETUP_FAILURE);
                return -1;
            default:
                m_device->driverSetupMessage(tr("Opening Capture Device %1 failed with unknown error type").
                                           arg(capture_pcm_name), AudioDevice::DRIVER_SETUP_WARNING);
            }

            capture_handle = nullptr;
        }

        if (capture_handle) {
            snd_pcm_nonblock (capture_handle, 0);
        }
    }

    if (playback_handle == nullptr) {
        if (playback) {

            if (capture_handle == nullptr) {
                /* can't do anything */
                m_device->driverSetupMessage(tr("Unable to configure Device %1").arg(pcmName), AudioDevice::DRIVER_SETUP_FAILURE);
                return -1;
            }

            /* they asked for playback, but we can't do it */
            m_device->driverSetupMessage(tr("Falling back to capture-only mode for device %1").arg(playback_pcm_name), AudioDevice::DRIVER_SETUP_WARNING);

            playback = false;
        }
    }

    if (capture_handle == nullptr) {
        if (capture) {

            if (playback_handle == nullptr) {
                /* can't do anything */
                m_device->driverSetupMessage(tr("Unable to configure Device %1").arg(pcmName), AudioDevice::DRIVER_SETUP_FAILURE);
                return -1;
            }

            /* they asked for capture, but we can't do it */
            m_device->driverSetupMessage(tr("Falling back to playback-only mode for device %1").arg(playback_pcm_name), AudioDevice::DRIVER_SETUP_WARNING);

            capture = false;
        }
    }


    if (playback_handle) {
        if ((err = snd_pcm_hw_params_malloc (&playback_hw_params)) < 0) {
            PWARN ("AlsaDriver: could not allocate playback hw params structure");
            return -1;
        }

        if ((err = snd_pcm_sw_params_malloc (&playback_sw_params)) < 0) {
            PWARN ("AlsaDriver: could not allocate playback sw params structure");
            return -1;
        }
    }

    if (capture_handle) {
        if ((err = snd_pcm_hw_params_malloc (&capture_hw_params)) < 0) {
            PWARN ("AlsaDriver: could not allocate capture hw params structure");
            return -1;
        }

        if ((err = snd_pcm_sw_params_malloc (&capture_sw_params)) < 0) {
            PWARN ("AlsaDriver: could not allocate capture sw params structure");
            return -1;
        }
    }



    if (set_parameters (m_framesPerCycle, user_nperiods, m_frameRate)) {
        return -1;
    }

    capture_and_playback_not_synced = false;

    if (capture_handle && playback_handle) {
        if (snd_pcm_link (playback_handle, capture_handle) != 0) {
            capture_and_playback_not_synced = true;
        }
    }


    return 1;
}



void AlsaDriver::release_channel_dependent_memory ()
{
    bitset_destroy (&channels_done);
    bitset_destroy (&channels_not_done);

    if (playback_addr) {
        free(playback_addr);
        playback_addr = nullptr;
    }

    if (capture_addr) {
        free(capture_addr);
        capture_addr = nullptr;
    }

    if (playback_interleave_skip) {
        free (playback_interleave_skip);
        playback_interleave_skip = nullptr;
    }

    if (capture_interleave_skip) {
        free (capture_interleave_skip);
        capture_interleave_skip = nullptr;
    }


    if (silent) {
        free(silent);
        silent = nullptr;
    }

    if (m_ditherState) {
        free(m_ditherState);
        m_ditherState = nullptr;
    }
}


void AlsaDriver::setup_io_function_pointers()
{
    switch (playback_sample_bytes) {
    case 2:
        if (playback_interleaved) {
            channel_copy = memcpy_interleave_d16_s16;
        } else {
            channel_copy = memcpy_fake;
        }

        switch (m_dither) {
        case Rectangular:
            fprintf (stderr,"Rectangular dithering at 16 bits\n");
            write_via_copy = quirk_bswap?
                        sample_move_dither_rect_d16_sSs:
                        sample_move_dither_rect_d16_sS;
            break;

        case Triangular:
            printf("Triangular dithering at 16 bits\n");
            write_via_copy = quirk_bswap?
                        sample_move_dither_tri_d16_sSs:
                        sample_move_dither_tri_d16_sS;
            break;

        case Shaped:
            printf("Noise-shaped dithering at 16 bits\n");
            write_via_copy = quirk_bswap?
                        sample_move_dither_shaped_d16_sSs:
                        sample_move_dither_shaped_d16_sS;
            break;

        default:
            write_via_copy = quirk_bswap?
                        sample_move_d16_sSs : sample_move_d16_sS;
            break;
        }
        break;

    case 3:
        if (playback_interleaved) {
            channel_copy = memcpy_interleave_d24_s24;
        } else {
            channel_copy = memcpy_fake;
        }

        switch (m_dither) {
        case Rectangular:
            printf("Rectangular dithering at 24 bits\n");
            write_via_copy = quirk_bswap?
                        sample_move_dither_rect_d24_sSs:
                        sample_move_dither_rect_d24_sS;
            break;

        case Triangular:
            printf("Triangular dithering at 24 bits\n");
            write_via_copy = quirk_bswap?
                        sample_move_dither_tri_d24_sSs:
                        sample_move_dither_tri_d24_sS;
            break;

        case Shaped:
            printf("Noise-shaped dithering at 24 bits\n");
            write_via_copy = quirk_bswap?
                        sample_move_dither_shaped_d24_sSs:
                        sample_move_dither_shaped_d24_sS;
            break;

        default:
            write_via_copy = quirk_bswap?
                        sample_move_d24_sSs : sample_move_d24_sS;
            break;
        }
        break;

    case 4:
        if (playback_interleaved) {
            channel_copy = memcpy_interleave_d32_s32;
        } else {
            channel_copy = memcpy_fake;
        }

        switch (m_dither) {
        case Rectangular:
            printf("Rectangular dithering at 32 bits\n");
            write_via_copy = quirk_bswap?
                        sample_move_dither_rect_d32u24_sSs:
                        sample_move_dither_rect_d32u24_sS;
            break;

        case Triangular:
            printf("Triangular dithering at 16 bits\n");
            write_via_copy = quirk_bswap?
                        sample_move_dither_tri_d32u24_sSs:
                        sample_move_dither_tri_d32u24_sS;
            break;

        case Shaped:
            printf("Noise-shaped dithering at 32 bits\n");
            write_via_copy = quirk_bswap?
                        sample_move_dither_shaped_d32u24_sSs:
                        sample_move_dither_shaped_d32u24_sS;
            break;

        default:
            write_via_copy = quirk_bswap?
                        sample_move_d32u24_sSs : sample_move_d32u24_sS;
            break;
        }
        break;
    }

    switch (capture_sample_bytes) {
    case 2:
        read_via_copy = quirk_bswap?
                    sample_move_dS_s16s : sample_move_dS_s16;
        break;
    case 3:
        read_via_copy = quirk_bswap?
                    sample_move_dS_s24s : sample_move_dS_s24;
        break;
    case 4:
        read_via_copy = quirk_bswap?
                    sample_move_dS_s32u24s : sample_move_dS_s32u24;
        break;
    }
}

int AlsaDriver::configure_stream(char *device_name,
                                 const char *stream_name,
                                 snd_pcm_t *handle,
                                 snd_pcm_hw_params_t *hw_params,
                                 snd_pcm_sw_params_t *sw_params,
                                 unsigned int *nperiodsp,
                                 unsigned long *nchns,
                                 unsigned long sample_width)
{
    int err, format;
    snd_pcm_uframes_t stop_th;
    static struct {
        char Name[32];
        snd_pcm_format_t format;
        int swapped;
        int bitdepth;
    } formats[] = {
    {"32bit little-endian", SND_PCM_FORMAT_S32_LE, IS_LE, 32},
    {"32bit big-endian", SND_PCM_FORMAT_S32_BE, IS_BE, 32},
    {"24bit little-endian", SND_PCM_FORMAT_S24_3LE, IS_LE, 24},
    {"24bit big-endian", SND_PCM_FORMAT_S24_3BE, IS_BE, 24},
    {"16bit little-endian", SND_PCM_FORMAT_S16_LE, IS_LE, 16},
    {"16bit big-endian", SND_PCM_FORMAT_S16_BE, IS_BE, 16},
};
#define NUMFORMATS (sizeof(formats)/sizeof(formats[0]))
#define FIRST_16BIT_FORMAT 4

    if ((err = snd_pcm_hw_params_any (handle, hw_params)) < 0)  {
        printf("AlsaDriver: no playback configurations available (%s)\n", snd_strerror (err));
        return -1;
    }

    if ((err = snd_pcm_hw_params_set_periods_integer (handle, hw_params))  < 0) {
        printf("AlsaDriver: cannot restrict period size to integral value.\n");
        return -1;
    }

    if ((err = snd_pcm_hw_params_set_access (handle, hw_params, SND_PCM_ACCESS_MMAP_NONINTERLEAVED)) < 0) {
        if ((err = snd_pcm_hw_params_set_access (handle, hw_params, SND_PCM_ACCESS_MMAP_INTERLEAVED)) < 0) {
            if ((err = snd_pcm_hw_params_set_access (handle, hw_params, SND_PCM_ACCESS_MMAP_COMPLEX)) < 0) {
                printf("AlsaDriver: mmap-based access is not possible for the %s "
                       "stream of this audio interface\n", stream_name);

                m_device->driverSetupMessage(tr("Memory-based access is not possible for Device %1, unable to configure driver").arg(device_name), AudioDevice::DRIVER_SETUP_FAILURE);

                return -1;
            }
        }
    }

    format = (sample_width == 4) ? 0 : (NUMFORMATS - 1);
    while (true) {
        if ((err = snd_pcm_hw_params_set_format ( handle, hw_params, formats[format].format)) < 0) {

            if (( (sample_width == 4) ? (format++ >= int(NUMFORMATS) - 1) : (format-- <= 0))) {
                printf("ALSA Driver: Sorry. The audio interface \"%s\" doesn't support any of the"
                       " hardware sample formats that Traverso's alsa-driver can use.\n", device_name);
                return -1;
            }
        } else {
            if (formats[format].swapped) {
                quirk_bswap = 1;
            } else {
                quirk_bswap = 0;
            }
            printf("AlsaDriver: final selected sample format for %s: %s\n", stream_name, formats[format].Name);
            m_device->set_bit_depth(formats[format].bitdepth);
            break;
        }
    }

    uint requestedFrameRate = m_frameRate;
    if ( (err = snd_pcm_hw_params_set_rate_near (handle, hw_params, &m_frameRate, NULL)) < 0) {
        printf("AlsaDriver: cannot set sample/frame rate to % for %s\n", (double)m_frameRate, stream_name);
        return -1;
    }

    if (requestedFrameRate != m_frameRate) {
        m_device->driverSetupMessage(tr("Requested framerate of %1 not supported by soundcard, setting to nearest framerate of %2 instead").
                                   arg(requestedFrameRate).arg(m_frameRate), AudioDevice::DRIVER_SETUP_WARNING);
    }

    if (!*nchns) {
        /*if not user-specified, try to find the maximum number of channels */
        unsigned int channels_max ;
        err = snd_pcm_hw_params_get_channels_max (hw_params, &channels_max);
        *nchns = channels_max;

        if (*nchns > 1024) {

            /* the hapless user is an unwitting victim of
            the "default" ALSA PCM device, which can
            support up to 16 million channels. since
            they can't be bothered to set up a proper
            default device, limit the number of
            channels for them to a sane default.
            */

            PERROR (
                        "ALSA Driver: You appear to be using the ALSA software \"plug\" layer, probably\n"
                        "a result of using the \"default\" ALSA device. This is less\n"
                        "efficient than it could be. Consider using a hardware device\n"
                        "instead rather than using the plug layer. Usually the name of the\n"
                        "hardware device that corresponds to the first sound card is hw:0\n"
                        );
            *nchns = 2;
        }
    }

    if ((err = snd_pcm_hw_params_set_channels (handle, hw_params, *nchns)) < 0) {
        printf("AlsaDriver: cannot set channel count to %lu for %s\n", *nchns, stream_name);
        return -1;
    }
    int frperscycle = m_framesPerCycle;
    if ((err = snd_pcm_hw_params_set_period_size (handle, hw_params, m_framesPerCycle, 0)) < 0) {
        printf("AlsaDriver: cannot set period size to %d frames for %s\n", frperscycle, stream_name);
        return -1;
    }

    *nperiodsp = user_nperiods;
    snd_pcm_hw_params_set_periods_min (handle, hw_params, nperiodsp, NULL);
    if (int(*nperiodsp) < user_nperiods)
        *nperiodsp = user_nperiods;

    if (snd_pcm_hw_params_set_periods_near (handle, hw_params, nperiodsp, NULL) < 0) {
        printf("AlsaDriver: cannot set number of periods to %u for %s\n", *nperiodsp, stream_name);
        return -1;
    }

    if (int(*nperiodsp) < user_nperiods) {
        printf("AlsaDriver: use %d periods for %s\n", *nperiodsp, stream_name);
        return -1;
    }

    if (!is_power_of_two(m_framesPerCycle)) {
        printf("Traverso: frames must be a power of two (64, 512, 1024, ...)\n");
        return -1;
    }

    if ((err = snd_pcm_hw_params_set_buffer_size (handle, hw_params,  *nperiodsp * m_framesPerCycle)) < 0) {
        printf("AlsaDriver: cannot set buffer length to %d for %s\n", *nperiodsp * m_framesPerCycle, stream_name);
        return -1;
    }

    if ((err = snd_pcm_hw_params (handle, hw_params)) < 0) {
        printf("AlsaDriver: cannot set hardware parameters for %s\n", stream_name);
        m_device->driverSetupMessage(tr("Unable to configure device %1, is it in use by another application?").
                                   arg(device_name), AudioDevice::DRIVER_SETUP_FAILURE);
        return -1;
    }

    snd_pcm_sw_params_current (handle, sw_params);

    if ((err = snd_pcm_sw_params_set_start_threshold (handle, sw_params, 0U)) < 0) {
        printf("AlsaDriver: cannot set start mode for %s\n", stream_name);
        return -1;
    }

    stop_th = *nperiodsp * m_framesPerCycle;
    if (soft_mode) {
        stop_th = (snd_pcm_uframes_t)-1;
    }

    if ((err = snd_pcm_sw_params_set_stop_threshold (handle, sw_params, stop_th)) < 0) {
        printf("AlsaDriver: cannot set stop mode for %s\n", stream_name);
        return -1;
    }

    if ((err = snd_pcm_sw_params_set_silence_threshold (handle, sw_params, 0)) < 0) {
        printf("AlsaDriver: cannot set silence threshold for %s\n", stream_name);
        return -1;
    }

#if 0
    fprintf (stderr, "set silence size to %lu * %lu = %lu\n",
             frames_per_cycle, *nperiodsp,
             frames_per_cycle * *nperiodsp);

    if ((err = snd_pcm_sw_params_set_silence_size (
             handle, sw_params,
             frames_per_cycle * *nperiodsp)) < 0) {
        PERROR ("AlsaDriver: cannot set silence size for %s",
                stream_name);
        return -1;
    }
#endif

    if (handle == playback_handle)
        err = snd_pcm_sw_params_set_avail_min (handle, sw_params, m_framesPerCycle * (*nperiodsp - user_nperiods + 1));
    else
        err = snd_pcm_sw_params_set_avail_min (handle, sw_params, m_framesPerCycle);

    if (err < 0) {
        printf("AlsaDriver: cannot set avail min for %s\n", stream_name);
        return -1;
    }

    if ((err = snd_pcm_sw_params (handle, sw_params)) < 0) {
        printf("AlsaDriver: cannot set software parameters for %s\n",	stream_name);
        return -1;
    }

    return 0;
}

int  AlsaDriver::set_parameters (nframes_t frames_per_interupt,
                                 int nperiods,
                                 nframes_t rate)
{
    int dir;
    snd_pcm_uframes_t p_period_size = 0;
    snd_pcm_uframes_t c_period_size = 0;
    channel_t chn;
    unsigned int pr = 0;
    unsigned int cr = 0;
    int err;

    m_frameRate = rate;
    m_framesPerCycle = frames_per_interupt;
    user_nperiods = nperiods;

    fprintf (stderr, "AlsaDriver: configuring for %d Hz, period=%ld frames (%.1f ms), buffer=%d periods\n",
             rate, (long)m_framesPerCycle,(((float)m_framesPerCycle / (float) rate) * 1000.0f), user_nperiods);

    QString configString("AlsaDriver: configuring for %1 Hz, period=%2 frames (%3 ms), buffer=%4 periods");
    configString = configString.arg(rate).arg(m_framesPerCycle).arg(((float)m_framesPerCycle / (float) rate) * 1000.0f, 0, 'g', 4).arg(user_nperiods);
    m_device->driverSetupMessage(configString, AudioDevice::DRIVER_SETUP_INFO);

    if (capture_handle) {
        if (configure_stream (
                    alsa_name_capture,
                    "capture",
                    capture_handle,
                    capture_hw_params,
                    capture_sw_params,
                    &capture_nperiods,
                    &capture_nchannels,
                    capture_sample_bytes)) {
            PERROR ("AlsaDriver: cannot configure capture channel");
            return -1;
        }
    }

    if (playback_handle) {
        if (configure_stream (
                    alsa_name_playback,
                    "playback",
                    playback_handle,
                    playback_hw_params,
                    playback_sw_params,
                    &playback_nperiods,
                    &playback_nchannels,
                    playback_sample_bytes)) {
            PERROR ("AlsaDriver: cannot configure playback channel");
            return -1;
        }
    }

    /* check the rate, since thats rather important */

    if (playback_handle) {
        snd_pcm_hw_params_get_rate (playback_hw_params, &pr, &dir);
    }

    if (capture_handle) {
        snd_pcm_hw_params_get_rate (capture_hw_params, &cr, &dir);
    }

    if (capture_handle && playback_handle) {
        if (cr != pr) {
            //			PERROR ("ALSA Driver: playback and capture sample rates do not match (%d vs. %d)", pr, cr);
        }

        /* only change if *both* capture and playback rates
        * don't match requested certain hardware actually
        * still works properly in full-duplex with slightly
        * different rate values between adc and dac
        */
        if (cr != m_frameRate && pr != m_frameRate) {
            //			PERROR ("ALSA Driver: sample rate in use (%d Hz) does not match requested rate (%d Hz)", cr, frame_rate);
            m_frameRate = cr;
        }

    } else if (capture_handle && cr != m_frameRate) {
        //		PERROR ("ALSA Driver: capture sample rate in use (%d Hz) does not match requested rate (%d Hz)", cr, frame_rate);
        m_frameRate = cr;
    } else if (playback_handle && pr != m_frameRate) {
        //		PERROR ("ALSA Driver: playback sample rate in use (%d Hz) does not match requested rate (%d Hz)", pr, frame_rate);
        m_frameRate = pr;
    }


    /* check the fragment size, since thats non-negotiable */

    if (playback_handle) {
        snd_pcm_access_t access;

        err = snd_pcm_hw_params_get_period_size (playback_hw_params, &p_period_size, &dir);
        err = snd_pcm_hw_params_get_format (playback_hw_params,	&playback_sample_format);
        err = snd_pcm_hw_params_get_access (playback_hw_params, &access);
        playback_interleaved = (access == SND_PCM_ACCESS_MMAP_INTERLEAVED)
                || (access == SND_PCM_ACCESS_MMAP_COMPLEX);

        if (p_period_size != m_framesPerCycle) {
            //			PERROR ("alsa_pcm: requested an interrupt every %ld frames but got %ld frames for playback", (long)frames_per_cycle, p_period_size);
            return -1;
        }
    }

    if (capture_handle) {
        snd_pcm_access_t access;

        err = snd_pcm_hw_params_get_period_size (capture_hw_params, &c_period_size, &dir);
        err = snd_pcm_hw_params_get_format (capture_hw_params, &(capture_sample_format));
        err = snd_pcm_hw_params_get_access (capture_hw_params, &access);
        capture_interleaved = (access == SND_PCM_ACCESS_MMAP_INTERLEAVED)
                || (access == SND_PCM_ACCESS_MMAP_COMPLEX);


        if (c_period_size != m_framesPerCycle) {
            //			PERROR ("alsa_pcm: requested an interrupt every %ld frames but got %ld frames for capture", (long)frames_per_cycle, p_period_size);
            return -1;
        }
    }

    playback_sample_bytes =	snd_pcm_format_physical_width(playback_sample_format)	/ 8;
    capture_sample_bytes =	snd_pcm_format_physical_width(capture_sample_format)	/ 8;

    if (playback_handle) {
        switch (playback_sample_format) {
        case SND_PCM_FORMAT_S32_LE:
        case SND_PCM_FORMAT_S24_3LE:
        case SND_PCM_FORMAT_S24_3BE:
        case SND_PCM_FORMAT_S16_LE:
        case SND_PCM_FORMAT_S32_BE:
        case SND_PCM_FORMAT_S16_BE:
            break;

        default:
            PERROR ("ALSA Driver: programming error: unhandled format type for playback");
            return -1;
        }
    }

    if (capture_handle) {
        switch (capture_sample_format) {
        case SND_PCM_FORMAT_S32_LE:
        case SND_PCM_FORMAT_S24_3LE:
        case SND_PCM_FORMAT_S24_3BE:
        case SND_PCM_FORMAT_S16_LE:
        case SND_PCM_FORMAT_S32_BE:
        case SND_PCM_FORMAT_S16_BE:
            break;

        default:
            PERROR ("ALSA Driver: programming error: unhandled format type for capture");
            return -1;
        }
    }

    if (playback_interleaved) {
        const snd_pcm_channel_area_t *my_areas;
        snd_pcm_uframes_t offset, frames;
        if (snd_pcm_mmap_begin(playback_handle, &my_areas, &offset, &frames) < 0) {
            //			PERROR ("AlsaDriver: %s: mmap areas info error", alsa_name_playback);
            return -1;
        }

        interleave_unit = snd_pcm_format_physical_width(playback_sample_format) / 8;
    } else {
        interleave_unit = 0;  /* NOT USED */
    }

    if (capture_interleaved) {
        const snd_pcm_channel_area_t *my_areas;
        snd_pcm_uframes_t offset, frames;
        if (snd_pcm_mmap_begin(capture_handle, &my_areas, &offset, &frames) < 0) {
            //			PERROR ("AlsaDriver: %s: mmap areas info error", alsa_name_capture);
            return -1;
        }
    }

    if (playback_nchannels > capture_nchannels) {
        max_nchannels = playback_nchannels;
        user_nchannels = capture_nchannels;
    } else {
        max_nchannels = capture_nchannels;
        user_nchannels = playback_nchannels;
    }

    setup_io_function_pointers ();

    /* Allocate and initialize structures that rely on the
    channels counts.

    Set up the bit pattern that is used to record which
    channels require action on every cycle. any bits that are
    not set after the engine's process() call indicate channels
    that potentially need to be silenced.
    */

    bitset_create (&channels_done, max_nchannels);
    bitset_create (&channels_not_done, max_nchannels);

    if (playback_handle) {
        playback_addr =  (char**) malloc (sizeof (char *) * playback_nchannels);
        memset (playback_addr, 0, sizeof (char *) * playback_nchannels);
        playback_interleave_skip = (unsigned long *) malloc (sizeof (unsigned long *) * playback_nchannels);
        memset (playback_interleave_skip, 0, sizeof (unsigned long *) * playback_nchannels);
        silent = (unsigned long *) malloc (sizeof (unsigned long) * playback_nchannels);

        for (chn = 0; chn < playback_nchannels; chn++) {
            silent[chn] = 0;
        }

        for (chn = 0; chn < playback_nchannels; chn++) {
            bitset_add (channels_done, chn);
        }

        m_ditherState = (dither_state_t *) calloc ( playback_nchannels, sizeof (dither_state_t));
    }

    if (capture_handle) {
        capture_addr = (char **) malloc (sizeof (char *) * capture_nchannels);
        memset (capture_addr, 0, sizeof (char *) * capture_nchannels);
        capture_interleave_skip = (unsigned long *) malloc (sizeof (unsigned long *) * capture_nchannels);
        memset (capture_interleave_skip, 0, sizeof (unsigned long *) * capture_nchannels);
    }

    m_periodUSecs = (trav_time_t) floor ((((float) m_framesPerCycle) / m_frameRate) * 1000000.0f);
    poll_timeout = (int) floor (1.5f * m_periodUSecs);

    return 0;
}

int AlsaDriver::reset_parameters (nframes_t frames_per_cycle,
                                  nframes_t user_nperiods,
                                  nframes_t rate)
{
    /* XXX unregister old ports ? */
    release_channel_dependent_memory ();
    return set_parameters (frames_per_cycle, user_nperiods, rate);
}

int AlsaDriver::get_channel_addresses (snd_pcm_uframes_t *capture_avail,
                                       snd_pcm_uframes_t *playback_avail,
                                       snd_pcm_uframes_t *capture_offset,
                                       snd_pcm_uframes_t *playback_offset)
{
    int err;
    channel_t chn;

    if (capture_avail) {
        if ((err = snd_pcm_mmap_begin (capture_handle, &capture_areas, capture_offset, capture_avail)) < 0) {
            //			PERROR ("AlsaDriver: %s: mmap areas info error", alsa_name_capture);
            return -1;
        }

        for (chn = 0; chn < capture_nchannels; chn++) {
            const snd_pcm_channel_area_t *a = &capture_areas[chn];
            capture_addr[chn] = (char *) a->addr + ((a->first + a->step * *capture_offset) / 8);
            capture_interleave_skip[chn] = (unsigned long ) (a->step / 8);
        }
    }

    if (playback_avail) {
        if ((err = snd_pcm_mmap_begin (playback_handle, &playback_areas, playback_offset, playback_avail)) < 0) {
            //			PERROR ("AlsaDriver: %s: mmap areas info error ", alsa_name_playback);
            return -1;
        }

        for (chn = 0; chn < playback_nchannels; chn++) {
            const snd_pcm_channel_area_t *a = &playback_areas[chn];
            playback_addr[chn] = (char *) a->addr + ((a->first + a->step * *playback_offset) / 8);
            playback_interleave_skip[chn] = (unsigned long ) (a->step / 8);
        }
    }

    return 0;
}

int AlsaDriver::start()
{
    int err;
    snd_pcm_uframes_t poffset, pavail;
    channel_t chn;

    poll_last = 0;
    poll_next = 0;

    if (playback_handle) {
        if ((err = snd_pcm_prepare (playback_handle)) < 0) {
            m_device->driverSetupMessage(QString("AlsaDriver: prepare error for playback on \"%s\" (%s)").arg(alsa_name_playback).arg(snd_strerror(err)), AudioDevice::DRIVER_SETUP_FAILURE);
            return -1;
        }
    }

    if ((capture_handle && capture_and_playback_not_synced)  || !playback_handle) {
        if ((err = snd_pcm_prepare (capture_handle)) < 0) {
            m_device->driverSetupMessage(QString("AlsaDriver: prepare error for capture on \"%s\" (%s)").arg(alsa_name_capture).arg(snd_strerror(err)), AudioDevice::DRIVER_SETUP_FAILURE);
            return -1;
        }
    }


    if (playback_handle) {
        playback_nfds = snd_pcm_poll_descriptors_count (playback_handle);
    } else {
        playback_nfds = 0;
    }

    if (capture_handle) {
        capture_nfds = snd_pcm_poll_descriptors_count (capture_handle);
    } else {
        capture_nfds = 0;
    }

    if (pfd) {
        free (pfd);
    }

    pfd = (struct pollfd *)	malloc (sizeof (struct pollfd) * (playback_nfds + capture_nfds + 2));

    if (playback_handle) {
        /* fill playback buffer with zeroes, and mark
        all fragments as having data.
        */

        pavail = snd_pcm_avail_update (playback_handle);

        if (pavail !=  m_framesPerCycle * playback_nperiods) {
            PERROR ("AlsaDriver: full buffer not available at start");
            return -1;
        }

        if (get_channel_addresses (nullptr, &pavail, nullptr, &poffset)) {
            return -1;
        }

        /* XXX this is cheating. ALSA offers no guarantee that
        we can access the entire buffer at any one time. It
        works on most hardware tested so far, however, buts
        its a liability in the long run. I think that
        alsa-lib may have a better function for doing this
        here, where the goal is to silence the entire
        buffer.
        */

        for (chn = 0; chn < playback_nchannels; chn++) {
            silence_on_channel (chn, user_nperiods * m_framesPerCycle);
        }

        snd_pcm_mmap_commit (playback_handle, poffset, user_nperiods * m_framesPerCycle);

        if ((err = snd_pcm_start (playback_handle)) < 0) {
            m_device->driverSetupMessage(QString("AlsaDriver: could not start playback (%1)").arg(snd_strerror (err)), AudioDevice::DRIVER_SETUP_FAILURE);
            return -1;
        }
    }

    if ((capture_handle && capture_and_playback_not_synced)  || !playback_handle) {
        if ((err = snd_pcm_start (capture_handle)) < 0) {
            m_device->driverSetupMessage(QString("AlsaDriver: could not start capture (%1)").arg(snd_strerror (err)), AudioDevice::DRIVER_SETUP_FAILURE);
            return -1;
        }
    }

    m_device->driverSetupMessage(tr("Succesfully started ALSA stream!"), AudioDevice::DRIVER_SETUP_SUCCESS);

    return 0;
}

int AlsaDriver::stop()
{
    int err;
    audio_sample_t* buf;

    /* silence all capture port buffers, because we might
    be entering offline mode.
    */

    for (int i=0; i<m_captureChannels.size(); ++i) {
        AudioChannel* chan = m_captureChannels.at(i);
        buf = chan->get_buffer(m_framesPerCycle);
        memset (buf, 0, sizeof (audio_sample_t) * m_framesPerCycle);
    }

    if (playback_handle) {
        if ((err = snd_pcm_drop (playback_handle)) < 0) {
            //                        PERROR ("AlsaDriver: channel flush for playback failed (%s)", snd_strerror (err));
            return -1;
        }
    }

    if (!playback_handle || capture_and_playback_not_synced) {
        if (capture_handle) {
            if ((err = snd_pcm_drop (capture_handle)) < 0) {
                //				PERROR ("AlsaDriver: channel flush for capture failed (%s)", snd_strerror (err));
                return -1;
            }
        }
    }


    return 0;
}

int AlsaDriver::restart()
{
    if (stop())
        return -1;
    return start();
}

int AlsaDriver::xrun_recovery (float *delayed_usecs)
{
    PWARN("xrun");
    snd_pcm_status_t *status;
    int res;

    snd_pcm_status_alloca(&status);

    if (capture_handle) {
        if ((res = snd_pcm_status(capture_handle, status))  < 0) {
            printf ("status error: %s", snd_strerror(res));
        }
    } else {
        if ((res = snd_pcm_status(playback_handle, status)) < 0) {
            printf ("status error: %s", snd_strerror(res));
        }
    }

    if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN && process_count > XRUN_REPORT_DELAY) {
        struct timeval now, diff, tstamp;
        gettimeofday(&now, nullptr);
        snd_pcm_status_get_trigger_tstamp(status, &tstamp);
        timersub(&now, &tstamp, &diff);
        *delayed_usecs = diff.tv_sec * 1000000 + diff.tv_usec;
        printf ("\n**** alsa_pcm: xrun of at least %.3f msecs\n\n", *delayed_usecs / 1000.0);
        m_device->xrun();
    }

    if (restart()) {
        return -1;
    }

    return 0;
}

void AlsaDriver::silence_untouched_channels (nframes_t nframes)
{
    channel_t chn;
    nframes_t buffer_frames = m_framesPerCycle * playback_nperiods;

    for (chn = 0; chn < playback_nchannels; chn++) {
        if (bitset_contains (channels_not_done, chn)) {
            if (silent[chn] < buffer_frames) {
                silence_on_channel_no_mark (chn, nframes);
                silent[chn] += nframes;
            }
        }
    }
}

static int under_gdb = false;

int AlsaDriver::wait(int extra_fd, int *status, float *delayed_usecs)
{
    snd_pcm_sframes_t avail = 0;
    snd_pcm_sframes_t capture_avail = 0;
    snd_pcm_sframes_t playback_avail = 0;
    int xrun_detected = false;
    int need_capture;
    int need_playback;
    unsigned int i;
    trav_time_t poll_enter;
    trav_time_t poll_ret = 0;

    *status = -1;
    *delayed_usecs = 0;

    need_capture = capture_handle ? 1 : 0;

    if (extra_fd >= 0) {
        need_playback = 0;
    } else {
        need_playback = playback_handle ? 1 : 0;
    }

again:

    while (need_playback || need_capture) {

        int poll_result;
        unsigned int ci = 0;
        unsigned int nfds;
        unsigned short revents;

        nfds = 0;

        if (need_playback) {
            snd_pcm_poll_descriptors (playback_handle, &pfd[0], playback_nfds);
            nfds += playback_nfds;
        }

        if (need_capture) {
            snd_pcm_poll_descriptors (capture_handle, &pfd[nfds], capture_nfds);
            ci = nfds;
            nfds += capture_nfds;
        }

        /* ALSA doesn't set POLLERR in some versions of 0.9.X */

        for (i = 0; i < nfds; i++) {
            pfd[i].events |= POLLERR;
        }

        if (extra_fd >= 0) {
            pfd[nfds].fd = extra_fd;
            pfd[nfds].events =
                    POLLIN|POLLERR|POLLHUP|POLLNVAL;
            nfds++;
        }

        poll_enter = TTimeRef::get_nanoseconds_since_epoch ();

        if (poll_enter > poll_next) {
            /*
            * This processing cycle was delayed past the
            * next due interrupt!  Do not account this as
            * a wakeup delay:
            */
            poll_next = 0;
        }

        m_device->set_transport_cycle_end_time(poll_enter);

        poll_result = poll (pfd, nfds, poll_timeout);
        if (poll_result < 0) {

            if (errno == EINTR) {
                printf ("poll interrupt\n");
                // this happens mostly when run
                // under gdb, or when exiting due to a signal
                if (under_gdb) {
                    goto again;
                }
                *status = -2;
                return 0;
            }

            //			PERROR ("AlsaDriver: poll call failed (%s)",
            //				strerror (errno));
            *status = -3;
            return 0;

        }

        poll_ret = TTimeRef::get_nanoseconds_since_epoch ();

        if (extra_fd < 0) {
            if (poll_next && poll_ret > poll_next) {
                *delayed_usecs = poll_ret - poll_next;
            }
            poll_last = poll_ret;
            poll_next = poll_ret + m_periodUSecs;
            m_device->set_transport_cycle_start_time (poll_ret);
        }

#ifdef DEBUG_WAKEUP
        fprintf (stderr, "%" PRIu64 ": checked %d fds, %" PRIu64
                 " usecs since poll entered\n", poll_ret, nfds,
                 poll_ret - poll_enter);
#endif

        /* check to see if it was the extra FD that caused us
        * to return from poll */

        if (extra_fd >= 0) {

            if (pfd[nfds-1].revents == 0) {
                /* we timed out on the extra fd */

                *status = -4;
                return -1;
            }

            /* if POLLIN was the only bit set, we're OK */

            *status = 0;
            return (pfd[nfds-1].revents == POLLIN) ? 0 : -1;
        }

        if (need_playback) {
            if (snd_pcm_poll_descriptors_revents(playback_handle, &pfd[0], playback_nfds, &revents) < 0) {
                qWarning("AlsaDriver: playback revents failed");
                *status = -6;
                return 0;
            }

            if (revents & POLLERR) {
                xrun_detected = true;
            }

            if (revents & POLLOUT) {
                need_playback = 0;

#ifdef DEBUG_WAKEUP

                fprintf (stderr, "%" PRIu64
                         " playback stream ready\n",
                         poll_ret);
#endif

            }
        }

        if (need_capture) {
            if (snd_pcm_poll_descriptors_revents(capture_handle, &pfd[ci], capture_nfds, &revents) < 0) {
                qWarning ("AlsaDriver: capture revents failed");
                *status = -6;
                return 0;
            }

            if (revents & POLLERR) {
                xrun_detected = true;
            }

            if (revents & POLLIN) {
                need_capture = 0;
#ifdef DEBUG_WAKEUP

                fprintf (stderr, "%" PRIu64
                         " capture stream ready\n",
                         poll_ret);
#endif

            }
        }

        if (poll_result == 0) {
            qWarning ("AlsaDriver: poll time out, polled for %ld usecs", (long)(poll_ret - poll_enter));
            *status = -5;
            return 0;
        }

    }

    if (capture_handle) {
        if ((capture_avail = snd_pcm_avail_update (capture_handle)) < 0) {
            if (capture_avail == -EPIPE) {
                xrun_detected = true;
            } else {
                //				PERROR ("ALSA Driver: unknown avail_update return value (%ld)", capture_avail);
            }
        }
    } else {
        /* odd, but see min() computation below */
        capture_avail = INT_MAX;
    }

    if (playback_handle) {
        if ((playback_avail = snd_pcm_avail_update (playback_handle)) < 0) {
            if (playback_avail == -EPIPE) {
                xrun_detected = true;
            } else {
                //				PERROR ("ALSA Driver: unknown avail_update return value (%ld)", playback_avail);
            }
        }
    } else {
        /* odd, but see min() computation below */
        playback_avail = INT_MAX;
    }

    if (xrun_detected) {
        *status = xrun_recovery (delayed_usecs);
        return 0;
    }

    *status = 0;
    m_lastWaitUsecond = poll_ret;

    avail = capture_avail < playback_avail ? capture_avail : playback_avail;

#ifdef DEBUG_WAKEUP

    fprintf (stderr, "wakeup complete, avail = %lu, pavail = %lu "
                     "cavail = %lu\n",
             avail, playback_avail, capture_avail);
#endif

    /* mark all channels not done for now. read/write will change this */

    bitset_copy (channels_not_done, channels_done);

    /* constrain the available count to the nearest (round down) number of
    periods.
    */

    return avail - (avail % m_framesPerCycle);
}

int AlsaDriver::_null_cycle(nframes_t nframes)
{
    nframes_t nf;
    snd_pcm_uframes_t offset;
    snd_pcm_uframes_t contiguous;
    uint chn;

    if (nframes > m_framesPerCycle) {
        return -1;
    }

    if (capture_handle) {
        nf = nframes;
        offset = 0;
        while (nf) {
            contiguous = nf;

            if (snd_pcm_mmap_begin (
                        capture_handle,
                        &capture_areas,
                        (snd_pcm_uframes_t *) &offset,
                        (snd_pcm_uframes_t *) &contiguous)) {
                return -1;
            }

            if (snd_pcm_mmap_commit (capture_handle, offset, contiguous) < 0) {
                return -1;
            }

            nf -= contiguous;
        }
    }

    if (playback_handle) {
        nf = nframes;
        offset = 0;
        while (nf) {
            contiguous = nf;

            if (snd_pcm_mmap_begin (
                        playback_handle,
                        &playback_areas,
                        (snd_pcm_uframes_t *) &offset,
                        (snd_pcm_uframes_t *) &contiguous)) {
                return -1;
            }

            for (chn = 0; chn < playback_nchannels; chn++) {
                silence_on_channel (chn, contiguous);
            }

            if (snd_pcm_mmap_commit (playback_handle, offset, contiguous) < 0) {
                return -1;
            }

            nf -= contiguous;
        }
    }

    return 0;
}

int AlsaDriver::bufsize(nframes_t nframes)
{
    return reset_parameters (nframes, user_nperiods,m_frameRate);
}

int AlsaDriver::_read(nframes_t nframes)
{
    snd_pcm_uframes_t contiguous;
    snd_pcm_uframes_t nread;
    snd_pcm_uframes_t offset;
    nframes_t  orig_nframes;
    audio_sample_t* buf;
    int err;

    if (nframes > m_framesPerCycle) {
        return -1;
    }

    if (!capture_handle) {
        return 0;
    }

    nread = 0;
    contiguous = 0;
    orig_nframes = nframes;

    while (nframes) {

        contiguous = nframes;

        if (get_channel_addresses (&contiguous, (snd_pcm_uframes_t *) nullptr, &offset, nullptr) < 0) {
            return -1;
        }

        for (int i=0; i<m_captureChannels.size(); ++i) {
            AudioChannel* channel = m_captureChannels.at(i);

            buf = channel->get_buffer(nframes);
            read_from_channel (channel->get_number(), buf + nread, contiguous);
            // FIXME: should AudioChannel::read_from_hardware_port() be modified
            // so that it also does partial buffer processing as we do here ?
            channel->read_from_hardware_port(buf, nframes);
        }

        if ((err = snd_pcm_mmap_commit (capture_handle, offset, contiguous)) < 0) {
            //			PERROR ("AlsaDriver: could not complete read of %ld frames: error = %d\n", contiguous, err);
            return -1;
        }

        nframes -= contiguous;
        nread += contiguous;
    }

    return 0;
}

int AlsaDriver::_write(nframes_t nframes)
{
    audio_sample_t* buf;
    nframes_t orig_nframes;
    snd_pcm_uframes_t nwritten;
    snd_pcm_uframes_t contiguous;
    snd_pcm_uframes_t offset;
    int err;

    process_count++;

    if (! playback_handle) {
        return 0;
    }

    if (nframes > m_framesPerCycle) {
        return -1;
    }

    nwritten = 0;
    contiguous = 0;
    orig_nframes = nframes;

    while (nframes) {

        contiguous = nframes;

        if (get_channel_addresses ((snd_pcm_uframes_t *) nullptr, &contiguous, nullptr, &offset) < 0) {
            return -1;
        }

        for (int i=0; i<m_playbackChannels.size(); ++i) {
            AudioChannel* channel = m_playbackChannels.at(i);
            buf = channel->get_buffer(nframes);
            write_to_channel (channel->get_number(), buf + nwritten, contiguous);
            channel->silence_buffer(nframes);
        }


        if (!bitset_empty (channels_not_done)) {
            silence_untouched_channels (contiguous);
        }

        if ((err = snd_pcm_mmap_commit (playback_handle, offset, contiguous)) < 0) {
            //			PERROR ("AlsaDriver: could not complete playback of %ld frames: error = %d", contiguous, err);
            if (err != EPIPE && err != ESTRPIPE)
                return -1;
        }

        nframes -= contiguous;
        nwritten += contiguous;
    }
    return 0;
}

int AlsaDriver::_run_cycle()
{
    int wait_status;
    float delayed_usecs;
    nframes_t nframes;

    nframes = wait (-1, &wait_status, &delayed_usecs);

    if (wait_status < 0) {
        return -1;		/* driver failed */
    }

    if (nframes == 0) {

        /* we detected an xrun and restarted: notify
        * clients about the delay.
        */
        m_device->delay(delayed_usecs);
        return 0;
    }

    return m_device->run_cycle (nframes, delayed_usecs);
}

int AlsaDriver::attach()
{
    channel_t chn;
    AudioChannel* chan;

    m_device->set_buffer_size (m_framesPerCycle);
    m_device->set_sample_rate (m_frameRate);


    for (chn = 0; chn < capture_nchannels; chn++) {
        QString channelName = QString("capture_%1").arg(chn+1);
        chan = add_capture_channel(channelName.toLatin1().data());
        chan->set_latency( m_framesPerCycle + m_captureFrameLatency );
    }

    for (chn = 0; chn < playback_nchannels; chn++) {
        QString channelName = QString("playback_%1").arg(chn+1);
        chan = add_playback_channel(channelName.toLatin1().data());
        chan->set_latency( m_framesPerCycle + m_captureFrameLatency );
    }


    return 1;
}

int AlsaDriver::detach ()
{
    return 0;
}

QString AlsaDriver::get_device_name( )
{
    return alsa_device_name(false, 0);
}

QString AlsaDriver::get_device_longname( )
{
    return alsa_device_name(true, 0);
}

QString AlsaDriver::alsa_device_longname(int devicenumber)
{
    return alsa_device_name(true, devicenumber);
}

QString AlsaDriver::alsa_device_name(int devicenumber)
{
    return alsa_device_name(false, devicenumber);
}

QString AlsaDriver::alsa_device_name(bool longname, int devicenumber)
{
    snd_ctl_card_info_t *info;
    snd_ctl_t *handle;
    QString deviceName = QString("hw:%1").arg(devicenumber);
    int err = 0;

    snd_ctl_card_info_alloca(&info);

    if ((err = snd_ctl_open(&handle, deviceName.toLatin1().data(), devicenumber)) < 0) {
        PMESG("AlsaDriver::alsa_device_name: Control open (device %i): %s", devicenumber, snd_strerror(err));
        return "";
    }

    if ((err = snd_ctl_card_info(handle, info)) < 0) {
        PMESG("Control hardware info (%i): %s", 0, snd_strerror(err));
    }

    snd_ctl_close(handle);

    if (err < 0) {
        return "Device name unknown";
    }


    if (longname) {
        return snd_ctl_card_info_get_mixername(info);
    }

    return snd_ctl_card_info_get_id(info);
}

int AlsaDriver::get_device_id_by_name(const QString& name)
{
    for (int i=0; i<6; i++) {
        if (alsa_device_name(false, i) == name) {
            return i;
        }
    }

    return -1;
}

//eof
