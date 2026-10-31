// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TByteSource.h"
#include "TAudioFileError.h"
#include "TAudioIOStats.h"
#include "TTimeRef.h"
#include "TTypes.h"

#include <QString>

#include <expected>
#include <memory>
#include <vector>

class TAudioBuffer;

/**
 * @brief Reads the audio of one file at the file's own rate. One class per
 * library (SFAudioReader: libsndfile, WPAudioReader: WavPack, MpgAudioReader:
 * MPEG audio through libmpg123, FaadAudioReader: AAC), and TScenarioDecoder
 * for a test signal described in a scenario file; opened by
 * open_audio_decoder().
 *
 * What a decoder does, and nothing else does for it: it keeps its position
 * in the file (next_frame(), in frames at the file's rate), moves it (seek()) and
 * reads from it (read()). The file's channels, rate and frames are set once,
 * when it is opened.
 *
 * The contract answers three questions:
 * - Opening: does it open? Yes, or no with the reason.
 * - Seeking: did the seek work? It landed, or not with the reason.
 * - After an error: go on with a new seek, or stop, because going on would
 *   give wrong audio.
 *
 * In full, which every decoder keeps and test_audio_decoders checks:
 * 1. read(buffer, count) fills count frames of every channel, from frame 0 of
 *    the buffer's channel buffers: what the file gives, zeros for the rest.
 *    It gives fewer frames than asked only at the end of the file or on an
 *    error.
 * 2. seek(frame) is only asked for a frame below frame_count(); a read at or
 *    past the end is handled above the decoder (read_from(),
 *    TAudioReader::read()): zeros and 0, no decoder call, no error. A seek
 *    that succeeds lands exactly: next_frame() is frame and the next read starts
 *    there, its samples equal to a read from the start (within the format's
 *    seek tolerance, MP3 and AAC). A seek that cannot go there says why (SeekFailed),
 *    leaves next_frame() unchanged and is an error.
 * 3. On an error (a failed seek, a failed read) the decoder is in its error
 *    state (in_error_state()): reads give zeros and 0 until a seek succeeds, which
 *    clears it, so a damaged part of a file can be skipped. A caller seeks
 *    again when the decoder is in its error state, also to next_frame().
 * 4. Audio that ends before frame_count() (a header that says more, a file
 *    cut short) is an error. A packet that does not decode (FAAD) stays
 *    silence, as the decoder decides: not an error.
 * 5. No stale audio: a caller never silences a buffer before a read, and
 *    never sees what the buffer held before; none reaches a ring buffer or
 *    a file.
 *
 * Each decoder ends its seek_frame() with seek_landed() or seek_failed() and
 * its read_frames() with read_done(), which keep points 3 and 4 and say why a
 * call failed (TAudioFileError): the error of that call, nothing is logged.
 *
 * A decoder that cannot open its file does not exist: every decoder has a
 * static open() that gives the decoder or the reason. It gets the file's
 * source from the probes, which is opened once.
 *
 * A decoder reaches its file through TByteSource only (source()), so
 * what it read and what it decoded can be told apart: io_stats().
 *
 * Not for the audio thread: file IO, and the buffer may grow.
 */
class TAudioDecoder
{
public:
    virtual ~TAudioDecoder() = default;

    // Of the file, at its own rate. No channels or no frames: it could not
    // be read
    const QString& file_name() const { return m_fileName; }
    uint channel_count() const { return m_channels; }
    uint rate() const { return m_rate; }
    nframes_t frame_count() const { return m_frames; }
    TTimeRef length() const { return TTimeRef(m_frames, m_rate); }
    // The library, for messages
    virtual QString decoder_type() const = 0;

    // The frame the next read() starts at
    nframes_t next_frame() const { return m_nextFrame; }
    bool at_end() const { return m_nextFrame >= m_frames; }
    // A seek or a read failed, and no seek succeeded since (point 3)
    bool in_error_state() const { return m_inErrorState; }
    // The next read() reads frame: the decoder is there and not in its
    // error state. Otherwise a read from frame seeks first (point 3).
    bool next_read_is_at(nframes_t frame) const { return m_nextFrame == frame && !m_inErrorState; }

    // To frame, which is below frame_count(); why not when the decoder
    // cannot go there (next_frame() is then unchanged)
    [[nodiscard]] TAudioFileStatus seek(nframes_t frame);
    // count frames from next_frame() into buffer, as the contract above says:
    // the frames read from the file (next_frame() moves on by as many), and the
    // error when this read failed
    TAudioFileResult read(TAudioBuffer& buffer, nframes_t count);

    // count frames from first: a seek only when next_frame() is elsewhere or the
    // decoder is in its error state (a seek costs: mpg123 and FAAD decode
    // frames before the target), then read(). At or past the end zeros and
    // 0, no error; where the decoder cannot seek to, zeros, 0 and the seek's
    // error.
    TAudioFileResult read_from(TAudioBuffer& buffer, nframes_t first, nframes_t count);

    // What it read and decoded since it opened
    const TAudioIOStats& io_stats() const { return m_stats; }
    // The open is over, what it cost is the decoder's: time, calls and reads,
    // the probes' included. From now on it counts its own seeks and reads
    void adopt_open_cost(const TAudioIOCost& cost);

protected:
    TAudioDecoder(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener);

    // The decoder's seek() and read(), as the contract above says
    [[nodiscard]] virtual TAudioFileStatus seek_frame(nframes_t frame) = 0;
    virtual TAudioFileResult read_frames(TAudioBuffer& buffer, nframes_t count) = 0;

    // The bytes of the file, for as long as the decoder lives
    TByteSource& source() { return *m_sources.front(); }
    // A second file of this one (WavPack's correction file), for as long as
    // the decoder lives, counted in io_stats(); nullptr when it is not there
    TByteSource* open_extra_source(const QString& fileName);
    // The end of every decoder's open: channels, a rate and frames, or why
    // not (a decoder without one of them reads nothing, and a rate of 0 would
    // be divided by)
    TAudioFileStatus has_audio() const;
    // The end of every seek_frame() that landed: at frame, no error
    TAudioFileStatus seek_landed(nframes_t frame);
    // The end of every seek_frame() that did not: the error state, next_frame()
    // unchanged; detail says why (the source's words where a read failed,
    // else the library's where it has any)
    TAudioFileStatus seek_failed(nframes_t frame, const QString& detail = QString());
    // The end of every read_frames(): zeros past the frames read, next_frame()
    // moved on by them, and fewer frames than asked before frame_count() an
    // error, with detail where the decoder knows why (a failed read of the
    // source goes before it)
    TAudioFileResult read_done(TAudioBuffer& buffer, nframes_t read, nframes_t count, const QString& detail = QString());

    // Set by the decoder when it opens the file
    QString m_fileName;
    uint m_channels{0};
    uint m_rate{0};
    nframes_t m_frames{0};
    // Moved by seek_frame() and read_frames() only, through seek_landed(),
    // seek_failed() and read_done()
    nframes_t m_nextFrame{0};
    bool m_inErrorState{false};

private:
    // Why a read of a source failed since the last call, empty when none did
    QString take_source_error();

    TByteSourceOpener m_opener;
    std::vector<std::unique_ptr<TByteSource>> m_sources;
    TAudioIOStats m_stats;
};

// The decoder for fileName, open, or why there is none: NotFound
// when the file is not there, else CannotOpen (no permission, no decoder for
// its format, the decoder's own reason). A scenario file (.tscenario) is
// known by its name and read by TScenarioDecoder; other files go by their
// content, see find_decoder().
// opener gives the file's bytes, also to the probes (tests).
[[nodiscard]] std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError> open_audio_decoder(const QString& fileName, const TByteSourceOpener& opener = {});

// Whether one of the decoders can read fileName. The file is only probed: no
// decoder is kept, nothing is logged.
bool can_decode_audio(const QString& fileName);
