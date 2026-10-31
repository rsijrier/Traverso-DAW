// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TByteSource.h"

#include "TClock.h"

#include <QFile>

#include <algorithm>
#include <utility>

#if defined(Q_OS_LINUX)
#include <cerrno>
#include <cstring>
#include <system_error>
#include <sys/uio.h>
#include <unistd.h>
#endif

#if defined(Q_OS_UNIX)
#include <sys/stat.h>
#include <sys/types.h>

static_assert(sizeof(off_t) == 8, "a file offset of 32 bits cuts positions past 2 GiB off");
#endif

int64_t TByteSource::read(void* data, int64_t bytes)
{
    if (bytes <= 0) {
        return 0;
    }
    char* destination = static_cast<char*>(data);

    int64_t filled = copy_from_block(m_position, destination, bytes);
    const int64_t rest = bytes - filled;
    bool failed = false;
    if (rest >= BlockBytes) {
        const TMediumRead direct = read_and_count(m_position + filled, destination + filled, rest);
        filled += direct.bytes;
        failed = direct.reason.has_value();
    } else if (rest > 0) {
        failed = !fill_block(m_position + filled);
        filled += copy_from_block(m_position + filled, destination + filled, rest);
    }

    m_position += filled;
    return (filled > 0 || !failed) ? filled : -1;
}

// The block is the file as it was when it was read: a file that grows reads
// on past it, one that shrinks still gives the block's bytes
int64_t TByteSource::copy_from_block(int64_t position, char* destination, int64_t bytes) const
{
    const int64_t inBlock = position - m_blockStart;
    if (inBlock < 0 || inBlock >= m_blockBytes) {
        return 0;
    }
    const int64_t count = std::min(bytes, m_blockBytes - inBlock);
    std::copy_n(m_block.data() + inBlock, count, destination);
    return count;
}

bool TByteSource::fill_block(int64_t position)
{
    m_block.resize(BlockBytes);
    m_blockStart = position;
    const TMediumRead read = read_and_count(position, m_block.data(), BlockBytes);
    m_blockBytes = read.bytes;
    return !read.reason;
}

TMediumRead TByteSource::read_and_count(int64_t position, char* data, int64_t bytes)
{
    TReadsByOrigin* reads = m_reads;

    const uint64_t start = TClock::now_nanoseconds();
    const std::optional<int64_t> cached = read_cached(position, data, bytes);
    const uint64_t afterCached = TClock::now_nanoseconds();
    TMediumRead result{cached.value_or(0), std::nullopt};
    if (reads && result.bytes > 0) {
        reads->cached.add(uint64_t(result.bytes), afterCached - start);
    }

    if (result.bytes < bytes) {
        TMediumRead medium = read_medium(position + result.bytes, data + result.bytes, bytes - result.bytes);
        // Every read counts, also one that gave nothing: its wait is the
        // interesting part on a stalling medium
        if (reads) {
            TReadTotals& kind = cached ? reads->medium : reads->unclassified;
            kind.add(uint64_t(medium.bytes), TClock::now_nanoseconds() - afterCached);
            // A read that met an error, also one that gave some bytes first
            if (medium.reason) {
                ++kind.failed;
            }
        }
        result.bytes += medium.bytes;
        result.reason = std::move(medium.reason);
    }

    // The first reason waits for take_error(): its user asks at the end of its
    // call, after several reads, and the first failure is the cause
    if (result.reason && !m_pendingError) {
        m_pendingError = result.reason;
    }
    return result;
}

bool TByteSource::seek(int64_t position)
{
    if (position < 0) {
        return false;
    }
    m_position = position;
    return true;
}

std::optional<QString> TByteSource::take_error()
{
    return std::exchange(m_pendingError, std::nullopt);
}

std::optional<int64_t> TByteSource::read_cached(int64_t, void*, int64_t)
{
    return std::nullopt;
}

// The bytes of a file on a disk or a network
class TFileByteSource : public TByteSource
{
public:
    bool open(const QString& fileName)
    {
        m_file.setFileName(fileName);
        // Unbuffered: the reads below are the reads of the medium
        return m_file.open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    QString open_error() const { return m_file.errorString(); }

    int64_t size() const override { return m_file.size(); }

    uint64_t medium_id() const override
    {
#if defined(Q_OS_UNIX)
        struct stat status;
        if (fstat(m_file.handle(), &status) == 0) {
            return uint64_t(status.st_dev);
        }
#endif
        // Windows: 0, every file looks like one medium. Planning per medium
        // (T71 step 2c, step 4) needs the volume there:
        // GetFileInformationByHandle(). Untested on macOS (fstat, as Linux).
        return 0;
    }

protected:
#if defined(Q_OS_LINUX)
    // preadv2/RWF_NOWAIT: glibc 2.26 and kernel headers 4.14 or later
    // (Debian has both); ENOSYS or another error falls back to unclassified reads
    std::optional<int64_t> read_cached(int64_t position, void* data, int64_t bytes) override
    {
        if (!m_canTell) {
            return std::nullopt;
        }
        iovec vector{data, size_t(bytes)};
        const ssize_t read = preadv2(m_file.handle(), &vector, 1, position, RWF_NOWAIT);
        if (read >= 0) {
            return read;
        }
        if (errno == EAGAIN) {
            return 0;
        }
        // Not on this file system (or kernel): the reads stay unclassified
        m_canTell = false;
        return std::nullopt;
    }

    TMediumRead read_medium(int64_t position, void* data, int64_t bytes) override
    {
        char* destination = static_cast<char*>(data);
        int64_t filled = 0;
        while (filled < bytes) {
            const ssize_t read = pread(m_file.handle(), destination + filled, size_t(bytes - filled), position + filled);
            if (read == 0) {
                break;
            }
            if (read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                // Not strerror(): it may share one buffer between threads
                return {filled, QString::fromStdString(std::generic_category().message(errno))};
            }
            filled += read;
        }
        return {filled, std::nullopt};
    }
#else
    // macOS and Windows: QFile::seek() + read(). Untested on those platforms
    // (no compiler or CI for them here); its logic was tested on Linux with
    // the Linux branches switched off (2026-10-30). No cache probe: every read counts as a medium read.
    TMediumRead read_medium(int64_t position, void* data, int64_t bytes) override
    {
        if (!m_file.seek(position)) {
            return {0, m_file.errorString()};
        }
        const int64_t read = m_file.read(static_cast<char*>(data), bytes);
        if (read < 0) {
            return {0, m_file.errorString()};
        }
        return {read, std::nullopt};
    }
#endif

private:
    QFile m_file;
#if defined(Q_OS_LINUX)
    bool m_canTell{true};
#endif
};

TByteSourceResult TByteSource::open_file(const QString& fileName)
{
    auto source = std::make_unique<TFileByteSource>();
    if (!source->open(fileName)) {
        // Asked after the failed open, not before: a check before is a race
        // and costs a system call on every open
        if (!QFile::exists(fileName)) {
            return std::unexpected(TByteSourceError{true, QStringLiteral("no such file")});
        }
        return std::unexpected(TByteSourceError{false, source->open_error()});
    }
    return source;
}
