// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TAudioDecoder.h"

typedef struct mpg123_handle_struct mpg123_handle;

// What libmpg123 reads from: the file's bytes, and how far it got
struct MpgInput
{
    TByteSource* source{nullptr};
    qint64 furthest{0};
};

/**
 * @brief MPEG audio (Layer I, II and III) through libmpg123, also inside a WAV
 * file (the format of doc/design/mp3-decoder.md).
 *
 * The constructor opens the file; scan() is a step of its own and gives the
 * length, so a later open can take the scan's results from a saved index
 * (mpg123_set_index64()) instead. That saved data must hold the scanned
 * samples as well: they are the seek limit, and without a LAME tag
 * mpg123_length64() stays an estimate.
 *
 * A damaged file reads exactly up to the damage and fails there: no resync,
 * which would shift the audio behind it, and no seek past what the scan found.
 *
 * Delay and padding are handled here, not by mpg123's gapless code: that
 * switches itself off when the scan finds fewer frames than the tag promises,
 * which shifts the audio of a file cut short.
 *
 * Seeks decode PREFRAMES frames before the target to fill the bit reservoir:
 * 12 for MPEG-1, 90 for MPEG-2 and 2.5. The known limit: 90 covers the 85
 * frames a stereo stream of 24 kHz at 8 kbps can reach back, 3 bytes per frame.
 * With CRC it is 1 byte per frame, so 255 frames back, and a seek there can
 * start with a silent frame.
 *
 * The environment variable TRAVERSO_MPG123_DECODER chooses mpg123's decoder
 * (one of mpg123_decoders()). The test runs its MP3 lines again with "generic"
 * this way; unset, mpg123 chooses the best one for the CPU.
 *
 * The one remaining thread risk: on ARM mpg123_new() finds NEON with a
 * process-wide SIGILL handler and a shared jmpbuf. Our own calls take a mutex;
 * libsndfile calls mpg123_new() itself, for a WAV with MP3 in it or an MP3 the
 * strict probe refused, and only says what it found after it opened the file.
 * A mutex around sf_open_virtual() would be held during IO, and a slow NAS
 * would hold up all opens. A crash is only possible on armhf without NEON, with
 * such a file opening at the same moment as another thread's mpg123_new().
 * aarch64 always has NEON, so no signal comes there.
 */
class MpgAudioReader : public TAudioDecoder
{
public:
    explicit MpgAudioReader(const QString& filename, const TByteSourceOpener& opener = {});
    ~MpgAudioReader() override;

    QString decoder_type() const override { return "mpg123"; }

    // The strict probe: whether the source holds MPEG audio. Opening alone
    // gives false hits on random bytes, so it walks the frames
    static bool can_decode(TByteSource& source);

    // Reads through the whole file: the frames, the seek limit and an index
    // for the seeks. Why not in open_error()
    void scan();

    TAudioFileStatus seek_frame(nframes_t frame) override;
    TAudioFileResult read_frames(TAudioBuffer& fileIOBuffer, nframes_t count) override;

private:
    void open_failed(const QString& detail);
    // The file's format is still the one it opened with
    bool same_format();

    mpg123_handle* m_handle{nullptr};
    MpgInput m_input;
    // The encoder's delay and padding of the LAME tag, -1 without one
    long m_delay{-1};
    long m_padding{-1};
    // What the tag says, an estimate from the file size without one
    int64_t m_tagSamples{0};
    // What the scan found: no seek goes at or past it
    int64_t m_scannedSamples{0};
    // Frame 0 of this reader in mpg123's samples
    int64_t m_skip{0};
};
