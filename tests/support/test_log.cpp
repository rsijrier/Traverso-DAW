// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TLog: records from several threads come out in time
// order with their thread names, nothing is lost below the buffer size, a
// full buffer drops and reports, and real-time records format in the drain.

#include "TLog.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

static QStringList lines_of(const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringList();
    }
    return QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
}

int main()
{
    QTemporaryDir directory;

    // Deferred formatting, without the drain
    {
        TLog::Record record{};
        record.format = "cycle {} took {:.2f} ms of {} {{braces}} {}";
        record.argCount = 4;
        record.args[0].kind = TLog::Arg::Kind::UInt; record.args[0].u = 42;
        record.args[1].kind = TLog::Arg::Kind::Double; record.args[1].d = 1.23456;
        record.args[2].kind = TLog::Arg::Kind::Str; record.args[2].s = "budget";
        record.args[3].kind = TLog::Arg::Kind::Bool; record.args[3].i = 1;
        check(TLog::format_deferred(record) == "cycle 42 took 1.23 ms of budget {braces} true", "deferred record formats its arguments");
        record.format = "{:z}";
        record.argCount = 1;
        check(TLog::format_deferred(record).find("format error") != std::string::npos, "a bad format spec is reported, not thrown");
    }

    // Four threads, 200 records each, before and while the drain runs
    {
        TLog::set_level(TLogLevel::Debug);
        TLog::name_thread("test");
        TLOG_I("test", "before the drain started {}", QString("works"));
        check(TLog::start(directory.path(), false, "test"), "the log file opens");

        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([t]() {
                char name[12];
                std::snprintf(name, sizeof(name), "worker%d", t);
                TLog::name_thread(name);
                for (int i = 0; i < 200; ++i) {
                    TLOG_RT_I("test", "worker {} record {}", t, i);
                    if (i % 50 == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        TLOG_W("test", "last one");
        TLog::stop();

        const QStringList lines = lines_of(TLog::file_name());
        check(lines.size() == 1 + 1 + 800 + 1, "header, 802 records: nothing lost");
        check(lines.size() > 2 && lines.at(1).contains("before the drain started works"), "a record from before start() is first");
        check(lines.last().contains(" W test") && lines.last().endsWith("last one"), "the last record is last");

        bool ordered = true;
        bool namesSeen = true;
        double previous = -1.0;
        for (int i = 1; i < lines.size(); ++i) {
            const QStringList fields = lines.at(i).simplified().split(' ');
            // fields: date time seconds thread [cycle] level category message...
            const double seconds = fields.at(2).toDouble();
            if (seconds < previous) {
                ordered = false;
            }
            previous = seconds;
            if (lines.at(i).contains("worker") && !fields.at(3).startsWith("worker")) {
                namesSeen = false;
            }
        }
        check(ordered, "lines are in time order across threads");
        check(namesSeen, "worker records carry their thread name");
        check(lines.at(1).contains(" test "), "the main thread is named");
    }

    // A full buffer drops and the drain reports how many
    {
        std::thread producer([]() {
            TLog::name_thread("flood");
            for (int i = 0; i < 1000; ++i) {
                TLOG_RT_I("test", "flood {}", i);
            }
        });
        producer.join();
        TLog::start(directory.path(), false, "test");
        TLog::stop();
        const QStringList lines = lines_of(TLog::file_name());
        int flood = 0;
        bool reported = false;
        for (const QString& line : lines) {
            if (line.contains("flood ") && line.contains(" I test")) ++flood;
            if (line.contains("744 records of this thread dropped")) reported = true;
        }
        check(flood == 256, "a full buffer keeps its 256 records");
        check(reported, "the dropped records are reported");
    }

    // Qt messages go through the log
    {
        TLog::start(directory.path(), false, "test");
        qWarning("qt warning %d", 7);
        TLog::stop();
        const QStringList lines = lines_of(TLog::file_name());
        bool found = false;
        for (const QString& line : lines) {
            if (line.contains(" W qt ") && line.endsWith("qt warning 7")) found = true;
        }
        check(found, "qWarning lands in the log as a warning of category qt");
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all log tests passed\n");
    return 0;
}
