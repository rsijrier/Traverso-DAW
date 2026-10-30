// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TByteSource.h"

#include <QFile>

#include <algorithm>
#include <chrono>
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

quint64 TByteSource::now_nanoseconds()
{
    return quint64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

qint64 TByteSource::read(void* data, qint64 bytes)
{
    if (bytes <= 0) {
        return 0;
    }
    char* destination = static_cast<char*>(data);
    qint64 got = 0;

    const auto from_block = [&] {
        const qint64 inBlock = m_position + got - m_blockStart;
        if (inBlock >= 0 && inBlock < m_blockBytes) {
            const qint64 count = std::min(bytes - got, m_blockBytes - inBlock);
            std::copy_n(m_block.data() + inBlock, count, destination + got);
            got += count;
        }
    };

    from_block();
    if (got < bytes && bytes - got >= BlockBytes) {
        got += transfer(m_position + got, destination + got, bytes - got);
    } else if (got < bytes) {
        m_block.resize(BlockBytes);
        m_blockStart = m_position + got;
        m_blockBytes = transfer(m_blockStart, m_block.data(), BlockBytes);
        from_block();
    }

    m_position += got;
    return got > 0 || !m_error ? got : -1;
}

qint64 TByteSource::transfer(qint64 position, char* data, qint64 bytes)
{
    TByteStats* stats = m_stats;

    const quint64 start = now_nanoseconds();
    const std::optional<qint64> cached = read_cached(position, data, bytes);
    const quint64 afterCached = now_nanoseconds();
    qint64 got = cached.value_or(0);
    if (stats && got > 0) {
        stats->cached.add(quint64(got), afterCached - start);
    }

    if (got < bytes) {
        // Per read: the error stays until take_error(), a decoder takes it
        // only at the end of its call, after several reads
        m_readFailed = false;
        const qint64 waited = read_medium(position + got, data + got, bytes - got);
        // Every read counts, also one that gave nothing: its wait is the
        // interesting part on a stalling medium
        if (stats) {
            TByteTransfer& kind = cached ? stats->medium : stats->untold;
            kind.add(quint64(std::max<qint64>(waited, 0)), now_nanoseconds() - afterCached);
            // A read that met an error, also one that gave some bytes first
            if (m_readFailed) {
                ++kind.failed;
            }
        }
        if (waited > 0) {
            got += waited;
        }
    }
    return got;
}

bool TByteSource::seek(qint64 position)
{
    if (position < 0) {
        return false;
    }
    m_position = position;
    return true;
}

std::optional<QString> TByteSource::take_error()
{
    return std::exchange(m_error, std::nullopt);
}

std::optional<qint64> TByteSource::read_cached(qint64, void*, qint64)
{
    return std::nullopt;
}

void TByteSource::read_failed(const QString& reason)
{
    m_error = reason;
    m_readFailed = true;
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

    qint64 size() const override { return m_file.size(); }

    quint64 medium_id() const override
    {
#if defined(Q_OS_UNIX)
        struct stat status;
        if (fstat(m_file.handle(), &status) == 0) {
            return quint64(status.st_dev);
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
    // (Debian has both); ENOSYS or another error falls back to untold reads
    std::optional<qint64> read_cached(qint64 position, void* data, qint64 bytes) override
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
        // Not on this file system (or kernel): the reads stay untold
        m_canTell = false;
        return std::nullopt;
    }

    qint64 read_medium(qint64 position, void* data, qint64 bytes) override
    {
        char* destination = static_cast<char*>(data);
        qint64 got = 0;
        while (got < bytes) {
            const ssize_t read = pread(m_file.handle(), destination + got, size_t(bytes - got), position + got);
            if (read == 0) {
                break;
            }
            if (read < 0) {
                if (errno == EINTR) {
                    continue;
                }
                // Not strerror(): it may share one buffer between threads
                read_failed(QString::fromStdString(std::generic_category().message(errno)));
                break;
            }
            got += read;
        }
        return got;
    }
#else
    // macOS and Windows: QFile::seek() + read(). Untested on those platforms
    // (no compiler or CI for them here); its logic was tested on Linux with
    // the Linux branches switched off (2026-10-30: audio_decoders and
    // stream_seek pass). No cache probe: every read counts as a medium read.
    qint64 read_medium(qint64 position, void* data, qint64 bytes) override
    {
        if (!m_file.seek(position)) {
            read_failed(m_file.errorString());
            return 0;
        }
        const qint64 read = m_file.read(static_cast<char*>(data), bytes);
        if (read < 0) {
            read_failed(m_file.errorString());
            return 0;
        }
        return read;
    }
#endif

private:
    QFile m_file;
#if defined(Q_OS_LINUX)
    bool m_canTell{true};
#endif
};

std::unique_ptr<TByteSource> TByteSource::open_file(const QString& fileName)
{
    auto source = std::make_unique<TFileByteSource>();
    if (!source->open(fileName)) {
        return nullptr;
    }
    return source;
}
