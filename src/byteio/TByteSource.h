// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// byteio (README.md). The goal: all disk access of Traverso goes through this
// library, so every read and write is known, timed and counted per medium.
// Today: reading audio files (audiofileio); writing audio, peak files and
// project files do not go through it yet. Next users: peak files, the write
// direction (recording, bounce) and video, on this library, with no second
// byte layer. It knows bytes, positions and the medium only: no frames, no
// audio types, no decoder phases.

#include "TByteStats.h"

#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

/**
 * @brief The bytes of one file: the only way a decoder reaches the medium.
 * It knows bytes, positions, time and the medium, nothing of audio.
 *
 * The libraries pull their bytes themselves, in the middle of a decode, so the
 * line between reading and decoding runs here, below the decoders: libsndfile
 * libmpg123 and WavPack through their virtual IO, FAAD directly. Every read is
 * counted and timed into a TByteStats, which its user (a decoder) points at
 * the kind of work going on.
 *
 * Small reads go through a block of BlockBytes: the parsers of MP4 and ADTS
 * read a few bytes at a time, tens of thousands of times when a file opens.
 *
 * A subclass gives the bytes; a test can give them from anywhere, with any
 * failure (TByteSourceOpener).
 *
 * One owner at a time, not for the audio thread: file IO.
 */
class TByteSource
{
public:
    virtual ~TByteSource() = default;

    // fileName for reading, nullptr when it does not open
    static std::unique_ptr<TByteSource> open_file(const QString& fileName);

    static constexpr qint64 BlockBytes = 16 * 1024;

    // The clock of the statistics
    static quint64 now_nanoseconds();

    // bytes from position() on, which moves on by what was read: fewer at the
    // end of the file or on a failure (take_error()), -1 when a failure gave
    // nothing
    qint64 read(void* data, qint64 bytes);
    // Moves position() only, the next read goes there; false below 0
    bool seek(qint64 position);
    qint64 position() const { return m_position; }
    bool at_end() const { return m_position >= size(); }

    // As the file is now: it can shrink while it is read
    virtual qint64 size() const = 0;
    // The medium the file is on, 0 where unknown
    virtual quint64 medium_id() const { return 0; }

    // Why a read failed since the last call
    std::optional<QString> take_error();

    // Where the reads are counted; it outlives this, nullptr: not counted
    void set_stats(TByteStats* stats) { m_stats = stats; }

protected:
    // What is there without waiting for the medium: up to bytes from position
    // into data, 0 for nothing. No value: the platform cannot tell.
    virtual std::optional<qint64> read_cached(qint64 position, void* data, qint64 bytes);
    // Up to bytes from position into data, waiting for the medium: fewer only
    // at the end of the file or on a failure, which goes to read_failed()
    virtual qint64 read_medium(qint64 position, void* data, qint64 bytes) = 0;
    void read_failed(const QString& reason);

private:
    // Up to bytes from position, from the medium, counted
    qint64 transfer(qint64 position, char* data, qint64 bytes);

    qint64 m_position{0};
    TByteStats* m_stats{nullptr};
    std::optional<QString> m_error;
    // Set by read_failed() during the read transfer() makes: that read failed
    bool m_readFailed{false};
    // The bytes of the last small read, from m_blockStart on
    std::vector<char> m_block;
    qint64 m_blockStart{0};
    qint64 m_blockBytes{0};
};

// Opens the bytes of a file for a decoder, nullptr when they are not there. An
// empty one opens the file (TByteSource::open_file()); tests give their
// own.
using TByteSourceOpener = std::function<std::unique_ptr<TByteSource>(const QString& fileName)>;
