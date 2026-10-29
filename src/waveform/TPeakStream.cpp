// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TPeakStream.h"

#include "TPeakCache.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <limits>

TPeakStream::TPeakStream(uint rate, int channelCount, QString peakPath)
    : m_rate(rate)
    , m_channelCount(channelCount)
    , m_peakPath(std::move(peakPath))
    , m_min(size_t(channelCount), std::numeric_limits<float>::max())
    , m_max(size_t(channelCount), std::numeric_limits<float>::lowest())
    , m_sumSquares(size_t(channelCount), 0.0f)
    , m_upper(size_t((TPeakLevels::chunk_records(TPeakLevels::ChunkLevel) - (qint64(1) << TPeakLevels::ChunkLevel))
                     * TPeakLevels::record_codes(channelCount)), 0)
    , m_upperFill(size_t(TPeakLevels::ChunkLevel + 1), 0)
    , m_waiting(size_t(TPeakLevels::ChunkLevel))
    , m_squares(size_t(channelCount), 0.0)
{
    Q_ASSERT(rate > 0 && channelCount > 0);
    for (Waiting& waiting : m_waiting) {
        waiting.minMax.resize(size_t(2 * channelCount));
        waiting.squares.resize(size_t(channelCount));
    }
    // In use as long as it lives: the cache's eviction leaves it alone. (A
    // peak file younger than an hour stays anyway.)
    TPeakCache::opened(m_peakPath);
}

TPeakStream::~TPeakStream()
{
    // A part file that never got its name (a build whose project went
    // first, a recording that was not post-processed) is of no use to anyone
    m_file.close();
    if (!published() && !m_partPath.isEmpty()) {
        QFile::remove(m_partPath);
    }
    TPeakCache::closed(m_peakPath);
}

bool TPeakStream::fail(const QString& error)
{
    m_failed = true;
    m_error = error;
    m_file.close();
    return false;
}

void TPeakStream::abandon()
{
    m_file.close();
    if (!published() && !m_partPath.isEmpty()) {
        QFile::remove(m_partPath);
    }
    m_failed = true;
}

TPeakFile::Status TPeakStream::open(QString* error)
{
    if (!QDir().mkpath(QFileInfo(m_peakPath).absolutePath())) {
        fail(QStringLiteral("cannot create the peak file directory"));
    } else {
        // Per process and per writer, and never one that is there already:
        // two writers of the same peak file (two Traverso instances, or an
        // old and a new build of a project that was opened again) never
        // write into each other's part file, and on a cache shared by two
        // machines a process id can come twice
        static std::atomic<quint64> writers{0};
        bool opened = false;
        for (int attempt = 0; attempt < 100 && !opened; ++attempt) {
            m_partPath = m_peakPath + QStringLiteral(".%1-%2.part").arg(QCoreApplication::applicationPid()).arg(writers.fetch_add(1) + 1);
            m_file.setFileName(m_partPath);
            opened = m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly);
            if (!opened && !QFile::exists(m_partPath)) {
                break;   // not because it exists: no other name helps
            }
        }
        // The header comes last: zeros until then, so an unfinished file is Invalid
        const QByteArray zeros(int(TPeakFile::HeaderBytes), '\0');
        if (!opened) {
            m_partPath.clear();
            fail(m_file.errorString());
        } else if (m_file.write(zeros) != zeros.size()) {
            fail(m_file.errorString());
        }
    }
    if (m_failed && error) {
        *error = m_error;
    }
    return m_failed ? TPeakFile::Status::Error : TPeakFile::Status::Ok;
}

void TPeakStream::append(const float* const* channels, qint64 frames)
{
    if (m_failed) {
        return;
    }

    qint64 position = 0;
    while (position < frames) {
        const int count = int(std::min<qint64>(frames - position, TPeakLevels::BlockFrames - m_blockFill));
        for (int c = 0; c < m_channelCount; ++c) {
            const float* samples = channels[c] + position;
            float low = m_min[size_t(c)];
            float high = m_max[size_t(c)];
            float squares = m_sumSquares[size_t(c)];
            for (int i = 0; i < count; ++i) {
                low = std::min(low, samples[i]);
                high = std::max(high, samples[i]);
                squares += samples[i] * samples[i];
            }
            m_min[size_t(c)] = low;
            m_max[size_t(c)] = high;
            m_sumSquares[size_t(c)] = squares;
        }
        position += count;
        m_blockFill += count;
        if (m_blockFill == TPeakLevels::BlockFrames) {
            close_block();
        }
    }
    m_frames += frames;
    write_records();
}

void TPeakStream::close_block()
{
    if (m_blockFill == 0) {
        return;
    }
    // Min and max of every channel, then the RMS of every channel; a partial
    // last block weighs its own frames
    for (int c = 0; c < m_channelCount; ++c) {
        m_records.push_back(TPeakLevels::code_at_or_below(m_min[size_t(c)]));
        m_records.push_back(TPeakLevels::code_at_or_above(m_max[size_t(c)]));
    }
    for (int c = 0; c < m_channelCount; ++c) {
        double meanSquare = double(m_sumSquares[size_t(c)]) / double(m_blockFill);
        if (!std::isfinite(meanSquare)) {
            // A NaN, an inf or a square too large for a float among the
            // samples: this block gets its peak (min and max leave NaN out),
            // clamped to the code range, so the blocks above, made from it,
            // stay finite. One broken sample (a plugin, a float file) does not
            // take the RMS of the whole source along.
            const float low = m_min[size_t(c)];
            const float high = m_max[size_t(c)];
            const float peak = low <= high ? std::max(std::fabs(low), std::fabs(high)) : 0.0f;
            const double bounded = std::min(peak, TPeakLevels::max_value());
            meanSquare = bounded * bounded;
        }
        m_records.push_back(TPeakLevels::code_nearest(float(std::sqrt(meanSquare))));
        m_min[size_t(c)] = std::numeric_limits<float>::max();
        m_max[size_t(c)] = std::numeric_limits<float>::lowest();
        m_sumSquares[size_t(c)] = 0.0f;
    }

    // The levels above it, from the decoded codes: a recording makes the
    // same file as a build from its audio
    const qint16* record = m_records.data() + m_records.size() - size_t(TPeakLevels::record_codes(m_channelCount));
    for (int c = 0; c < m_channelCount; ++c) {
        const double rms = TPeakLevels::magnitude_value(std::max(int(record[2 * m_channelCount + c]), 0));
        m_squares[size_t(c)] = rms * rms;
    }
    add_block(0, record, m_squares.data(), m_blockFill);
    m_blockFill = 0;
    if (++m_chunkBlocks == (qint64(1) << TPeakLevels::ChunkLevel)) {
        close_chunk();
    }
}

// A block of the chunk being made, on level: above level 0 it is one of the
// chunk's records; with the block that waits on its level it makes the block
// above it, up to the top level
void TPeakStream::add_block(int level, const qint16* minMax, const double* meanSquares, qint64 frames)
{
    constexpr int Top = TPeakLevels::ChunkLevel;
    if (level > 0) {
        // Every stored RMS is rounded once, from the mean squares of the
        // decoded level-0 codes
        const qint64 index = TPeakLevels::level_offset(Top, level) - (qint64(1) << Top) + m_upperFill[size_t(level)]++;
        qint16* record = m_upper.data() + index * TPeakLevels::record_codes(m_channelCount);
        std::copy_n(minMax, 2 * m_channelCount, record);
        for (int c = 0; c < m_channelCount; ++c) {
            record[2 * m_channelCount + c] = TPeakLevels::code_nearest(float(std::sqrt(meanSquares[c])));
        }
    }
    if (level == Top) {
        return;
    }
    Waiting& w = m_waiting[size_t(level)];
    if (!w.waiting) {
        std::copy_n(minMax, 2 * m_channelCount, w.minMax.begin());
        std::copy_n(meanSquares, m_channelCount, w.squares.begin());
        w.frames = frames;
        w.waiting = true;
        return;
    }

    // The pair makes the block above it
    w.waiting = false;
    const qint64 pairFrames = w.frames + frames;
    for (int c = 0; c < m_channelCount; ++c) {
        w.minMax[size_t(2 * c)] = std::min(w.minMax[size_t(2 * c)], minMax[2 * c]);
        w.minMax[size_t(2 * c + 1)] = std::max(w.minMax[size_t(2 * c + 1)], minMax[2 * c + 1]);
        w.squares[size_t(c)] = (w.squares[size_t(c)] * double(w.frames) + meanSquares[c] * double(frames)) / double(pairFrames);
    }
    add_block(level + 1, w.minMax.data(), w.squares.data(), pairFrames);
}

// The chunk's level 0 is complete: its levels above follow it in the file,
// and the next chunk starts
void TPeakStream::close_chunk()
{
    m_records.insert(m_records.end(), m_upper.begin(), m_upper.end());
    std::fill(m_upper.begin(), m_upper.end(), qint16(0));
    std::fill(m_upperFill.begin(), m_upperFill.end(), qint64(0));
    m_chunkBlocks = 0;
}

bool TPeakStream::write_records()
{
    if (m_records.empty()) {
        return true;
    }
    // Out of QFile's buffer too: a reader reads the file
    const qint64 bytes = qint64(m_records.size() * sizeof(qint16));
    if (m_file.write(reinterpret_cast<const char*>(m_records.data()), bytes) != bytes || !m_file.flush()) {
        return fail(m_file.errorString());   // nothing written from here on
    }
    m_records.clear();
    m_writtenFrames.store(m_frames - m_blockFill, std::memory_order_release);
    return true;
}

TPeakFile::Status TPeakStream::finish(const QString& audioPath, QString* error)
{
    if (!m_failed) {
        close_block();
        if (m_chunkBlocks > 0) {
            // The last chunk is not full. A block without its pair covers the
            // end of the source alone and moves up as it is, level by level.
            for (int level = 0; level < TPeakLevels::ChunkLevel; ++level) {
                Waiting& w = m_waiting[size_t(level)];
                if (w.waiting) {
                    w.waiting = false;
                    add_block(level + 1, w.minMax.data(), w.squares.data(), w.frames);
                }
            }
            // Zeros for the blocks the chunk does not have: every chunk has the same size
            const qint64 missing = (qint64(1) << TPeakLevels::ChunkLevel) - m_chunkBlocks;
            m_records.insert(m_records.end(), size_t(missing * TPeakLevels::record_codes(m_channelCount)), qint16(0));
            close_chunk();
        }
        if (write_records() && m_frames == 0) {
            fail(QStringLiteral("no audio"));
        }
    }
    if (m_failed) {
        // No peak file: the waveform builds it from the audio
        abandon();
        if (error) {
            *error = m_error;
        }
        return TPeakFile::Status::Error;
    }
    m_file.close();
    m_source = TPeakFile::SourceInfo::from_file(audioPath, m_rate, m_channelCount, m_frames);
    return TPeakFile::Status::Ok;
}

bool TPeakStream::post_process()
{
    if (m_failed || published()) {
        return false;
    }
    auto failed = [this](const QString& error) {
        fail(error);
        abandon();
        return false;
    };

    if (!m_headerWritten) {
        // The header last: the file is complete now
        m_file.setFileName(m_partPath);
        const QByteArray header = TPeakFile::header(m_source);
        if (!m_file.open(QIODevice::ReadWrite) || !m_file.seek(0) || m_file.write(header) != header.size() || !m_file.flush()) {
            return failed(m_file.errorString());
        }
        m_file.close();
        m_headerWritten = true;
    }

    // Its name. Windows renames no file that is open: a waveform maps it
    // for a moment per repaint, a virus scanner or the indexer may open it
    // too. A refused rename is tried again in the next steps (not in a
    // sleep: the step is part of a DiskIO round), for a while.
    QString error;
    if (TPeakFile::publish(m_partPath, m_peakPath, &error) != TPeakFile::Status::Ok) {
#if defined(Q_OS_WIN)
        if (!m_renaming.isValid()) {
            m_renaming.start();
        }
        if (m_renaming.elapsed() < 2000) {
            return true;
        }
#endif
        return failed(QStringLiteral("cannot rename the part file: %1").arg(error));
    }
    m_published.store(true, std::memory_order_release);
    return false;
}

void TPeakStream::read(const std::function<void(const TPeakLevels&)>& use) const
{
    constexpr int Top = TPeakLevels::ChunkLevel;
    const qint64 frames = written_frames();
    const qint64 blocks = (frames + TPeakLevels::BlockFrames - 1) >> TPeakLevels::BlockFramesShift;
    const qint64 chunks = blocks >> Top;
    const qint64 tail = blocks - (chunks << Top);
    const qint64 bytes = TPeakFile::HeaderBytes + chunks * TPeakFile::chunk_bytes(Top, m_channelCount) + tail * TPeakFile::record_bytes(m_channelCount);

    // No blocks yet: open() may still be choosing the part file's name on the
    // writing thread (a build's stream is handed out before its job runs), so
    // m_partPath is not read. Once there are blocks, the acquire of
    // written_frames() above orders it: open() set it before the first one.
    if (blocks == 0) {
        use(TPeakLevels(m_rate, m_channelCount, 0, Top, nullptr, 0));
        return;
    }

    // Opened per read, and mapped for this read only, so nothing stays open
    // that keeps Windows from renaming the file. Once renamed, from its name:
    // the records are in the same place.
    QFile file(m_partPath);
    bool opened = file.open(QIODevice::ReadOnly);
    if (!opened && published()) {
        file.setFileName(m_peakPath);
        opened = file.open(QIODevice::ReadOnly);
    }
    uchar* map = opened && file.size() >= bytes ? file.map(0, bytes) : nullptr;
    // The mapping does not need the descriptor
    file.close();
    if (!map) {
        use(TPeakLevels(m_rate, m_channelCount, 0, Top, nullptr, 0));
        return;
    }
    const auto* records = reinterpret_cast<const qint16*>(map + TPeakFile::HeaderBytes);
    use(TPeakLevels(m_rate, m_channelCount, frames, Top, records, chunks,
                    records + chunks * TPeakLevels::chunk_records(Top) * TPeakLevels::record_codes(m_channelCount)));
    file.unmap(map);
}
