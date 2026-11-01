// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TLog.h"

#include "TClock.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

std::atomic<TLogLevel> TLog::s_level{TLogLevel::Info};

// One thread's ring of records: the thread writes at head, the drain reads
// at tail. Both indexes only grow; the slot is index & (Capacity - 1)
struct TLogThreadBuffer
{
    static constexpr quint32 Capacity = 256;

    std::atomic<quint32> head{0};
    std::atomic<quint32> tail{0};
    std::atomic<quint32> dropped{0};
    std::atomic<bool>    named{false};
    char                 name[12]{};
    quint64              cycle{0};      // the owning thread only
    quint32              sequence{0};   // the owning thread only
    quint32              reportedDropped{0};   // the drain only
    TLog::Record         records[Capacity];
};

static constexpr int MaxThreads = 64;

static TLogThreadBuffer       s_buffers[MaxThreads];
static std::atomic<int>       s_claimed{0};
static std::atomic<quint32>   s_unclaimedDropped{0};

static thread_local TLogThreadBuffer* t_buffer = nullptr;
static thread_local bool              t_claimTried = false;

// The seconds in the log count from the start of the process, so records
// from before start() have no negative time
static const uint64_t s_processStartNs = TClock::now_nanoseconds();
static const int64_t s_processStartWallMs = QDateTime::currentMSecsSinceEpoch();

// The calling thread's buffer, claimed from the pool on the first call.
// nullptr when the pool is exhausted. Real-time safe: the thread_locals are
// constant initialised (no TLS allocation) and the claim is an atomic add
static TLogThreadBuffer* thread_buffer() T_REALTIME_SAFE
{
    TLogThreadBuffer* buffer = nullptr;
    bool claimTried = false;
    T_REALTIME_SAFE_BY_CONTRACT(buffer = t_buffer; claimTried = t_claimTried;)
    if (buffer || claimTried) {
        return buffer;
    }
    const int index = s_claimed.fetch_add(1, std::memory_order_acq_rel);
    buffer = index < MaxThreads ? &s_buffers[index] : nullptr;
    T_REALTIME_SAFE_BY_CONTRACT(t_claimTried = true; t_buffer = buffer;)
    return buffer;
}

void TLog::set_level(TLogLevel level)
{
    s_level.store(level, std::memory_order_relaxed);
}

bool TLog::level_from_name(std::string_view name, TLogLevel& level)
{
    if (name == "error") level = TLogLevel::Error;
    else if (name == "warning") level = TLogLevel::Warning;
    else if (name == "info") level = TLogLevel::Info;
    else if (name == "debug") level = TLogLevel::Debug;
    else if (name == "trace") level = TLogLevel::Trace;
    else return false;
    return true;
}

void TLog::name_thread(const char* name)
{
    TLogThreadBuffer* buffer = thread_buffer();
    if (!buffer || buffer->named.load(std::memory_order_relaxed)) {
        return;
    }
    std::strncpy(buffer->name, name, sizeof(buffer->name) - 1);
    buffer->name[sizeof(buffer->name) - 1] = '\0';
    buffer->named.store(true, std::memory_order_release);
}

void TLog::set_audio_cycle(quint64 cycle) T_REALTIME_SAFE
{
    if (TLogThreadBuffer* buffer = thread_buffer()) {
        buffer->cycle = cycle;
    }
}

quint64 TLog::audio_cycle() T_REALTIME_SAFE
{
    TLogThreadBuffer* buffer = thread_buffer();
    return buffer ? buffer->cycle : 0;
}

TLog::Record* TLog::begin_record(TLogLevel level, const char* category) T_REALTIME_SAFE
{
    TLogThreadBuffer* buffer = thread_buffer();
    if (!buffer) {
        s_unclaimedDropped.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    const quint32 head = buffer->head.load(std::memory_order_relaxed);
    if (head - buffer->tail.load(std::memory_order_acquire) >= TLogThreadBuffer::Capacity) {
        buffer->dropped.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    Record& record = buffer->records[head & (TLogThreadBuffer::Capacity - 1)];
    record.timeNs = TClock::now_nanoseconds();
    record.cycle = buffer->cycle;
    record.sequence = buffer->sequence++;
    record.level = level;
    record.argCount = 0;
    record.deferred = false;
    record.category = category;
    record.format = nullptr;
    record.text[0] = '\0';
    return &record;
}

void TLog::commit_record() T_REALTIME_SAFE
{
    TLogThreadBuffer* buffer = thread_buffer();
    buffer->head.store(buffer->head.load(std::memory_order_relaxed) + 1, std::memory_order_release);
}

void TLog::write_text(TLogLevel level, const char* category, const char* text)
{
    if (!enabled(level)) {
        return;
    }
    Record* record = begin_record(level, category);
    if (!record) {
        return;
    }
    std::strncpy(record->text, text, TextSize - 1);
    record->text[TextSize - 1] = '\0';
    commit_record();
}

void TLog::write_text(TLogLevel level, const char* category, const QString& text)
{
    if (!enabled(level)) {
        return;
    }
    write_text(level, category, text.toUtf8());
}

void TLog::write_text(TLogLevel level, const char* category, const QByteArray& text)
{
    write_text(level, category, text.constData());
}

void TLog::write_printf(TLogLevel level, const char* category, const char* fmt, ...)
{
    if (!enabled(level)) {
        return;
    }
    Record* record = begin_record(level, category);
    if (!record) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(record->text, TextSize, fmt, args);
    va_end(args);
    commit_record();
}

// ---------------------------------------------------------------- the drain

static std::string format_one(const TLog::Arg& arg, const std::string& spec)
{
    const std::string fmt = "{" + spec + "}";
    switch (arg.kind) {
    case TLog::Arg::Kind::Int:    return std::vformat(fmt, std::make_format_args(arg.i));
    case TLog::Arg::Kind::UInt:   return std::vformat(fmt, std::make_format_args(arg.u));
    case TLog::Arg::Kind::Double: return std::vformat(fmt, std::make_format_args(arg.d));
    case TLog::Arg::Kind::Bool:   { const bool b = arg.i != 0; return std::vformat(fmt, std::make_format_args(b)); }
    case TLog::Arg::Kind::Str:    { const char* s = arg.s ? arg.s : "(null)"; return std::vformat(fmt, std::make_format_args(s)); }
    case TLog::Arg::Kind::None:   break;
    }
    return "{?}";
}

std::string TLog::format_deferred(const Record& record)
{
    std::string out;
    const char* p = record.format ? record.format : "";
    int next = 0;
    try {
        while (*p) {
            if (p[0] == '{' && p[1] == '{') { out += '{'; p += 2; continue; }
            if (p[0] == '}' && p[1] == '}') { out += '}'; p += 2; continue; }
            if (*p == '{') {
                const char* end = std::strchr(p, '}');
                if (!end) { out += p; break; }
                std::string spec(p + 1, size_t(end - p - 1));
                if (next < record.argCount) {
                    out += format_one(record.args[next], spec);
                } else {
                    out += "{?}";
                }
                ++next;
                p = end + 1;
                continue;
            }
            out += *p++;
        }
    } catch (const std::format_error& error) {
        out += " (format error: ";
        out += error.what();
        out += ")";
    }
    return out;
}

struct TLogPending
{
    TLog::Record record;
    int          thread;
};

static std::thread            s_drain;
static std::atomic<bool>      s_running{false};
static std::atomic<bool>      s_stderr{false};
static FILE*                  s_file = nullptr;
static QString                s_fileName;
static qint64                 s_fileBytes = 0;
static uint64_t               s_anchorMonoNs = s_processStartNs;
static int64_t                s_anchorWallMs = s_processStartWallMs;
static std::vector<TLogPending> s_pending;

// A program that returns from main() without stop() must not destroy the
// joinable drain thread: destroyed before s_drain, this stops it first
static struct TLogShutdown
{
    ~TLogShutdown() { TLog::stop(); }
} s_shutdown;

static const char* thread_name(int index)
{
    const TLogThreadBuffer& buffer = s_buffers[index];
    if (buffer.named.load(std::memory_order_acquire)) {
        return buffer.name;
    }
    return nullptr;
}

static bool s_stderrIsTerminal = false;

// On a terminal the thread name gets a colour of its own, so the threads
// stand out in the stream; the file stays plain
static void write_line(const std::string& line, int thread)
{
    if (s_file) {
        std::fputs(line.c_str(), s_file);
        s_fileBytes += qint64(line.size());
    }
    if (s_stderr.load(std::memory_order_relaxed)) {
        if (s_stderrIsTerminal) {
            static const char* colours[] = {"\033[36m", "\033[33m", "\033[32m", "\033[35m", "\033[34m", "\033[91m", "\033[93m", "\033[96m"};
            // The thread column: after the date, the time and the seconds
            const size_t threadStart = 24 + 13;
            if (line.size() > threadStart + 12) {
                std::fputs(line.substr(0, threadStart).c_str(), stderr);
                std::fputs(colours[size_t(thread) % 8], stderr);
                std::fputs(line.substr(threadStart, 11).c_str(), stderr);
                std::fputs("\033[0m", stderr);
                std::fputs(line.substr(threadStart + 11).c_str(), stderr);
                return;
            }
        }
        std::fputs(line.c_str(), stderr);
    }
}

static std::string format_line(const TLogPending& pending)
{
    const TLog::Record& r = pending.record;
    const qint64 wallMs = s_anchorWallMs + (r.timeNs - s_anchorMonoNs) / 1000000;
    const QByteArray wall = QDateTime::fromMSecsSinceEpoch(wallMs).toString("yyyy-MM-dd HH:mm:ss.zzz").toLatin1();
    const double seconds = double(r.timeNs - s_anchorMonoNs) / 1.0e9;

    char thread[16];
    if (const char* name = thread_name(pending.thread)) {
        std::snprintf(thread, sizeof(thread), "%s", name);
    } else {
        std::snprintf(thread, sizeof(thread), "t%d", pending.thread);
    }

    char cycle[16] = "";
    if (r.cycle > 0) {
        std::snprintf(cycle, sizeof(cycle), "#%llu", static_cast<unsigned long long>(r.cycle));
    }

    static const char levels[] = {'E', 'W', 'I', 'D', 'T'};
    const char level = levels[std::min<int>(int(r.level), 4)];

    // The category up to a '.', so a file name reads as its class
    std::string category = r.category ? r.category : "";
    if (const size_t dot = category.find('.'); dot != std::string::npos) {
        category.resize(dot);
    }

    const std::string message = r.deferred ? TLog::format_deferred(r) : std::string(r.text);
    return std::format("{} {:12.6f} {:<11} {:>9} {} {:<14} {}\n", wall.constData(), seconds, thread, cycle, level, category, message);
}

// Moves the records the threads wrote since the last round into s_pending
// and writes out those older than holdNs, in time order. Younger ones wait a
// round so a slightly later record of another thread lands in its place
static void drain_round(qint64 holdNs)
{
    const int claimed = std::min(s_claimed.load(std::memory_order_acquire), MaxThreads);
    for (int i = 0; i < claimed; ++i) {
        TLogThreadBuffer& buffer = s_buffers[i];
        quint32 tail = buffer.tail.load(std::memory_order_relaxed);
        const quint32 head = buffer.head.load(std::memory_order_acquire);
        while (tail != head) {
            s_pending.push_back(TLogPending{buffer.records[tail & (TLogThreadBuffer::Capacity - 1)], i});
            ++tail;
            buffer.tail.store(tail, std::memory_order_release);
        }
        const quint32 dropped = buffer.dropped.load(std::memory_order_relaxed);
        if (dropped != buffer.reportedDropped) {
            TLogPending notice{};
            notice.thread = i;
            notice.record.timeNs = TClock::now_nanoseconds();
            notice.record.level = TLogLevel::Warning;
            notice.record.category = "log";
            std::snprintf(notice.record.text, TLog::TextSize, "%u records of this thread dropped, its log buffer was full",
                          dropped - buffer.reportedDropped);
            buffer.reportedDropped = dropped;
            s_pending.push_back(notice);
        }
    }

    const quint32 unclaimed = s_unclaimedDropped.exchange(0, std::memory_order_relaxed);
    if (unclaimed > 0) {
        TLogPending notice{};
        notice.thread = 0;
        notice.record.timeNs = TClock::now_nanoseconds();
        notice.record.level = TLogLevel::Warning;
        notice.record.category = "log";
        std::snprintf(notice.record.text, TLog::TextSize, "%u records dropped, more than %d threads log", unclaimed, MaxThreads);
        s_pending.push_back(notice);
    }

    std::stable_sort(s_pending.begin(), s_pending.end(), [](const TLogPending& a, const TLogPending& b) {
        if (a.record.timeNs != b.record.timeNs) return a.record.timeNs < b.record.timeNs;
        if (a.thread != b.thread) return a.thread < b.thread;
        return a.record.sequence < b.record.sequence;
    });

    const uint64_t limit = TClock::now_nanoseconds() - holdNs;
    size_t written = 0;
    while (written < s_pending.size() && s_pending[written].record.timeNs <= limit) {
        write_line(format_line(s_pending[written]), s_pending[written].thread);
        ++written;
    }
    s_pending.erase(s_pending.begin(), s_pending.begin() + qsizetype(written));

    if (s_file) {
        std::fflush(s_file);
    }
}

static void rotate_file()
{
    if (s_file) {
        std::fclose(s_file);
        s_file = nullptr;
    }
    const QString previous = s_fileName + ".1";
    QFile::remove(previous);
    QFile::rename(s_fileName, previous);
    s_file = std::fopen(s_fileName.toLocal8Bit().constData(), "w");
    s_fileBytes = 0;
}

static void drain_loop()
{
    TLog::name_thread("log");
    const qint64 holdNs = 5 * 1000000;
    while (s_running.load(std::memory_order_acquire)) {
        drain_round(holdNs);
        if (s_file && s_fileBytes > 10 * 1024 * 1024) {
            rotate_file();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Everything that is left, in order
    drain_round(0);
}

static void qt_message_handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    TLogLevel level = TLogLevel::Info;
    switch (type) {
    case QtDebugMsg:    level = TLogLevel::Debug; break;
    case QtInfoMsg:     level = TLogLevel::Info; break;
    case QtWarningMsg:  level = TLogLevel::Warning; break;
    case QtCriticalMsg:
    case QtFatalMsg:    level = TLogLevel::Error; break;
    }
    // Qt's category names are static strings; "default" says nothing
    const char* category = (context.category && std::strcmp(context.category, "default") != 0) ? context.category : "qt";
    TLog::write_text(level, category, message);
}

bool TLog::start(const QString& directory, bool alsoStderr, const char* version)
{
    if (s_running.load(std::memory_order_acquire)) {
        return true;
    }
    s_stderr.store(alsoStderr, std::memory_order_relaxed);
#if defined(Q_OS_UNIX)
    s_stderrIsTerminal = isatty(fileno(stderr)) != 0;
#endif

    if (!directory.isEmpty() && QDir().mkpath(directory)) {
        s_fileName = directory + "/traverso.log";
        rotate_file();
        if (!s_file) {
            std::fprintf(stderr, "TLog: cannot write %s\n", s_fileName.toLocal8Bit().constData());
        }
    }

    const std::string header = std::format("Traverso {} log, Qt {}, {} ({}), started {}\n",
                                           version ? version : "", qVersion(),
                                           QSysInfo::prettyProductName().toStdString(), QSysInfo::kernelVersion().toStdString(),
                                           QDateTime::currentDateTime().toString(Qt::ISODate).toStdString());
    write_line(header, 0);

    qInstallMessageHandler(qt_message_handler);

    s_running.store(true, std::memory_order_release);
    s_drain = std::thread(drain_loop);
    return s_file != nullptr;
}

void TLog::set_stderr(bool enabled)
{
    s_stderr.store(enabled, std::memory_order_relaxed);
}

void TLog::stop()
{
    if (!s_running.load(std::memory_order_acquire)) {
        return;
    }
    s_running.store(false, std::memory_order_release);
    s_drain.join();
    qInstallMessageHandler(nullptr);
    if (s_file) {
        std::fclose(s_file);
        s_file = nullptr;
    }
}

QString TLog::file_name()
{
    return s_fileName;
}

QString TLog::default_directory()
{
#if defined(Q_OS_MACOS)
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/Library/Logs/Traverso";
#elif defined(Q_OS_WIN)
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/logs";
#else
    // Not StateLocation: that depends on the application name being set already
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/Traverso";
#endif
}
