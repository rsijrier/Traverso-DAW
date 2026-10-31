// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include "TRealTime.h"

#include <atomic>
#include <cstddef>
#include <format>
#include <string_view>
#include <type_traits>
#include <utility>

enum class TLogLevel : quint8 { Error = 0, Warning = 1, Info = 2, Debug = 3, Trace = 4 };

/**
 * @brief The one log of Traverso: every thread writes records without
 *        waiting, one thread writes them out in time order.
 *
 * Each thread that logs gets its own ring buffer from a fixed pool on its
 * first record: writing is a few stores and one atomic, no lock, no
 * allocation, so the audio thread and driver callbacks can log too. A full
 * buffer drops the record and counts it. The drain thread merges the
 * buffers by time (steady clock) and writes to the log file and, when
 * enabled, stderr. Each line carries the wall clock time, the seconds since
 * start, the thread's name, the audio cycle for records from the audio
 * thread, the level, the category and the message, so a log of a user's
 * session can be read and parsed in the order things happened.
 *
 * Error, warning and info are always compiled in and written: the session
 * log. Debug and trace exist only in builds with TRAVERSO_DEV_LOG. The
 * runtime level (--log-level) decides what is recorded; records above it
 * cost one atomic load.
 *
 * TLOG_* formats at the call with std::format (checked at compile time);
 * QString and QByteArray arguments are supported. TLOG_RT_* records the
 * format string and up to six numbers or string literals and formats in
 * the drain thread: for the audio thread, which must not format.
 */
class TLog
{
public:
    static constexpr int    MaxArgs = 6;
    static constexpr size_t TextSize = 176;

    struct Arg
    {
        enum class Kind : quint8 { None, Int, UInt, Double, Str, Bool };
        Kind kind{Kind::None};
        union {
            qint64      i;
            quint64     u;
            double      d;
            const char* s;
        };
    };

    struct Record
    {
        uint64_t    timeNs{0};
        uint64_t    cycle{0};
        quint32     sequence{0};
        TLogLevel   level{TLogLevel::Info};
        quint8      argCount{0};
        bool        deferred{false};
        const char* category{nullptr};
        const char* format{nullptr};
        Arg         args[MaxArgs];
        char        text[TextSize];
    };

    // The runtime level: records above it are not recorded
    static TLogLevel level() T_REALTIME_SAFE { return s_level.load(std::memory_order_relaxed); }
    static void set_level(TLogLevel level);
    static bool enabled(TLogLevel level) T_REALTIME_SAFE { return quint8(level) <= quint8(TLog::level()); }
    // "error", "warning", "info", "debug", "trace"; false for anything else
    static bool level_from_name(std::string_view name, TLogLevel& level);

    // Names the calling thread in the log (at most 11 characters). Claims
    // the thread's buffer, so it is safe to call every time: only the first
    // call does anything
    static void name_thread(const char* name);
    // The audio thread tells which cycle it is processing; its records carry it
    static void set_audio_cycle(quint64 cycle) T_REALTIME_SAFE;
    // The cycle the calling thread set last, 0 in threads that never set one
    static quint64 audio_cycle() T_REALTIME_SAFE;

    // Formatted at the call, any thread but the audio thread
    template <class... Args>
    static void write(TLogLevel level, const char* category, std::format_string<Args...> fmt, Args&&... args)
    {
        if (!enabled(level)) {
            return;
        }
        Record* record = begin_record(level, category);
        if (!record) {
            return;
        }
        const auto result = std::format_to_n(record->text, TextSize - 1, fmt, std::forward<Args>(args)...);
        *result.out = '\0';
        commit_record();
    }

    static void write_text(TLogLevel level, const char* category, const char* text);
    static void write_text(TLogLevel level, const char* category, const QString& text);
    static void write_text(TLogLevel level, const char* category, const QByteArray& text);
    // printf style, for the Debugger macros
    static void write_printf(TLogLevel level, const char* category, const char* fmt, ...) Q_ATTRIBUTE_FORMAT_PRINTF(3, 4);

    // Real-time safe: fmt is a literal with {} placeholders (a format spec
    // like {:.2f} is allowed), args are numbers, bools or string literals.
    // Formatted in the drain thread
    template <class... Args>
    static void write_rt(TLogLevel level, const char* category, const char* fmt, Args... args) T_REALTIME_SAFE
    {
        static_assert(sizeof...(Args) <= MaxArgs, "TLOG_RT: at most six arguments");
        if (!enabled(level)) {
            return;
        }
        Record* record = begin_record(level, category);
        if (!record) {
            return;
        }
        record->deferred = true;
        record->format = fmt;
        record->argCount = quint8(sizeof...(Args));
        int index = 0;
        (set_arg(record->args[index++], args), ...);
        commit_record();
    }

    // The drain: the log file in directory (traverso.log, the previous one
    // kept as traverso.log.1) and optionally stderr. Records written before
    // start() are kept in the buffers and written then. version goes into
    // the header line
    static bool start(const QString& directory, bool alsoStderr, const char* version);
    // Where the log goes by default: the platform's place for logs and state
    // (Linux ~/.local/state/<app>, macOS ~/Library/Logs/<app>, Windows
    // AppData/Local/<app>/logs)
    static QString default_directory();
    static void set_stderr(bool enabled);
    static void stop();
    static QString file_name();

    // Formats a deferred record, for the drain and for tests
    static std::string format_deferred(const Record& record);

private:
    // Real-time safe: a slot of the calling thread's ring, claimed for the
    // thread on its first record, and its release to the drain
    static Record* begin_record(TLogLevel level, const char* category) T_REALTIME_SAFE;
    static void commit_record() T_REALTIME_SAFE;

    template <class T>
    static void set_arg(Arg& arg, T value) T_REALTIME_SAFE
    {
        using Type = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<Type, bool>) {
            arg.kind = Arg::Kind::Bool;
            arg.i = value ? 1 : 0;
        } else if constexpr (std::is_integral_v<Type> && std::is_signed_v<Type>) {
            arg.kind = Arg::Kind::Int;
            arg.i = qint64(value);
        } else if constexpr (std::is_integral_v<Type>) {
            arg.kind = Arg::Kind::UInt;
            arg.u = quint64(value);
        } else if constexpr (std::is_floating_point_v<Type>) {
            arg.kind = Arg::Kind::Double;
            arg.d = double(value);
        } else if constexpr (std::is_convertible_v<Type, const char*>) {
            arg.kind = Arg::Kind::Str;
            arg.s = value;
        } else {
            static_assert(!std::is_same_v<Type, Type>, "TLOG_RT: only numbers, bools and string literals, nothing that allocates");
        }
    }

    static std::atomic<TLogLevel> s_level;
};

// std::format support for Qt strings: {} of a QString or QByteArray
template <>
struct std::formatter<QString, char> : std::formatter<std::string_view, char>
{
    template <class Context>
    auto format(const QString& string, Context& context) const
    {
        const QByteArray utf8 = string.toUtf8();
        return std::formatter<std::string_view, char>::format(std::string_view(utf8.constData(), size_t(utf8.size())), context);
    }
};

template <>
struct std::formatter<QByteArray, char> : std::formatter<std::string_view, char>
{
    template <class Context>
    auto format(const QByteArray& bytes, Context& context) const
    {
        return std::formatter<std::string_view, char>::format(std::string_view(bytes.constData(), size_t(bytes.size())), context);
    }
};

// The session log: always compiled, always written
#define TLOG_E(category, ...) TLog::write(TLogLevel::Error, category, __VA_ARGS__)
#define TLOG_W(category, ...) TLog::write(TLogLevel::Warning, category, __VA_ARGS__)
#define TLOG_I(category, ...) TLog::write(TLogLevel::Info, category, __VA_ARGS__)
#define TLOG_RT_E(category, ...) TLog::write_rt(TLogLevel::Error, category, __VA_ARGS__)
#define TLOG_RT_W(category, ...) TLog::write_rt(TLogLevel::Warning, category, __VA_ARGS__)
#define TLOG_RT_I(category, ...) TLog::write_rt(TLogLevel::Info, category, __VA_ARGS__)

// The developer log: nothing in builds without TRAVERSO_DEV_LOG, not even
// the arguments
#ifdef TRAVERSO_DEV_LOG
#define TLOG_D(category, ...) TLog::write(TLogLevel::Debug, category, __VA_ARGS__)
#define TLOG_T(category, ...) TLog::write(TLogLevel::Trace, category, __VA_ARGS__)
#define TLOG_RT_D(category, ...) TLog::write_rt(TLogLevel::Debug, category, __VA_ARGS__)
#define TLOG_RT_T(category, ...) TLog::write_rt(TLogLevel::Trace, category, __VA_ARGS__)
#else
#define TLOG_D(category, ...) ((void)0)
#define TLOG_T(category, ...) ((void)0)
#define TLOG_RT_D(category, ...) ((void)0)
#define TLOG_RT_T(category, ...) ((void)0)
#endif
