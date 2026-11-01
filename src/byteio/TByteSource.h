// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// byteio (README.md): all disk access of Traverso goes through this library, so
// every read and write is known, timed and counted per medium.

#include "TReadsByOrigin.h"

#include <QString>

#include <cstdint>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class TByteSource;

// What one read of the medium did: the bytes, and why it stopped when it failed.
// Both at once: a read can give its first bytes and then fail. Fewer bytes than
// asked and no reason: the end of the file.
struct [[nodiscard]] TMediumRead
{
    int64_t bytes{0};
    std::optional<QString> reason;
};

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

    static constexpr int64_t BlockBytes = 16 * 1024;

    // bytes from position() on, which moves on by what was read: fewer at the
    // end of the file or on a failure (take_error()), -1 when a failure gave
    // nothing
    int64_t read(void* data, int64_t bytes);
    // Moves position() only, the next read goes there; false below 0
    bool seek(int64_t position);
    int64_t position() const { return m_position; }
    bool at_end() const { return m_position >= size(); }

    // As the file is now: it can shrink while it is read
    virtual int64_t size() const = 0;
    // The medium the file is on, 0 where unknown
    virtual uint64_t medium_id() const { return 0; }

    // Why a read failed since the last call
    std::optional<QString> take_error();

    // Where the reads are counted; it outlives this, nullptr: not counted
    void count_reads_into(TReadsByOrigin* reads) { m_reads = reads; }

protected:
    // What is there without waiting for the medium: up to bytes from position
    // into data, 0 for nothing. No value: the platform cannot tell.
    virtual std::optional<int64_t> read_cached(int64_t position, void* data, int64_t bytes);
    // Up to bytes from position into data, waiting for the medium: fewer only
    // at the end of the file or on a failure, which says why
    [[nodiscard]] virtual TMediumRead read_medium(int64_t position, void* data, int64_t bytes) = 0;

private:
    // Up to bytes from position, from the medium, counted
    TMediumRead read_and_count(int64_t position, char* data, int64_t bytes);
    // What the block holds of bytes from position on, copied to destination
    int64_t copy_from_block(int64_t position, char* destination, int64_t bytes) const;
    // The block from position on; false when the read failed
    bool fill_block(int64_t position);

    int64_t m_position{0};
    TReadsByOrigin* m_reads{nullptr};
    // Why a read failed, the first since take_error()
    std::optional<QString> m_pendingError;
    // The bytes of the last small read, from m_blockStart on
    std::vector<char> m_block;
    int64_t m_blockStart{0};
    int64_t m_blockBytes{0};
};

// Opens the bytes of a file for its user, or says why they are not there. An
// empty one opens the file (TByteSource::open_file()); tests give their own.
using TByteSourceOpener = std::function<TByteSourceResult(const QString& fileName)>;
