// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// byteio (README.md): all disk access of Traverso goes through this library, so
// every read and write is known, timed and counted per medium.

#include "TReadsByOrigin.h"

#include <QString>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class TByteSource;

// Why a file's bytes are not there
struct TByteSourceError
{
    // The file does not exist; otherwise it is there and does not open
    bool notFound;
    QString reason;
};

using TByteSourceResult = std::expected<std::unique_ptr<TByteSource>, TByteSourceError>;

/**
 * @brief The bytes of one file: the only way its user reaches the medium. It
 * knows bytes, positions, time and the medium.
 *
 * Every read is counted and timed into a TReadsByOrigin, which its user points
 * at the kind of work going on.
 *
 * Small reads go through a block of BlockBytes, for callers that read a few
 * bytes at a time, tens of thousands of times.
 *
 * A subclass gives the bytes; a test can give them from anywhere, with any
 * failure (TByteSourceOpener).
 *
 * One owner at a time, not for a real-time thread: file IO.
 */
class TByteSource
{
public:
    virtual ~TByteSource() = default;

    // fileName for reading, or why it does not open
    static TByteSourceResult open_file(const QString& fileName);

    static constexpr qint64 BlockBytes = 16 * 1024;

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
    void count_reads_into(TReadsByOrigin* reads) { m_reads = reads; }

protected:
    // What is there without waiting for the medium: up to bytes from position
    // into data, 0 for nothing. No value: the platform cannot tell.
    virtual std::optional<qint64> read_cached(qint64 position, void* data, qint64 bytes);
    // Up to bytes from position into data, waiting for the medium: fewer only
    // at the end of the file or on a failure, which goes to record_read_failure()
    virtual qint64 read_medium(qint64 position, void* data, qint64 bytes) = 0;
    void record_read_failure(const QString& reason);

private:
    // Up to bytes from position, from the medium, counted
    qint64 read_and_count(qint64 position, char* data, qint64 bytes);

    qint64 m_position{0};
    TReadsByOrigin* m_reads{nullptr};
    std::optional<QString> m_pendingError;
    // Set by record_read_failure() during the read read_and_count() makes: that read failed
    bool m_readFailed{false};
    // The bytes of the last small read, from m_blockStart on
    std::vector<char> m_block;
    qint64 m_blockStart{0};
    qint64 m_blockBytes{0};
};

// Opens the bytes of a file for its user, or says why they are not there. An
// empty one opens the file (TByteSource::open_file()); tests give their own.
using TByteSourceOpener = std::function<TByteSourceResult(const QString& fileName)>;
