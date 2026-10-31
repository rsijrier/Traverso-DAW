// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the scenario decoder, through TAudioReader as every
// reader of audio files uses it: every signal against its reference
// (test_signals.h), the ramp exact to the frame, the version guard, a
// scenario of no frames and a broken one refused, the facts of a scenario at
// rates that do and do not divide the universal rate, one frame, many
// channels, and the ramp written to a float WAV and read back exactly.

#include "TAudioReader.h"
#include "TAudioWriter.h"
#include "TAudioBuffer.h"
#include "TTimeRef.h"
#include "test_signals.h"

#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>
#include <tuple>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

static QString write_file(const QTemporaryDir& dir, const char* name, const QByteArray& content)
{
    const QString path = dir.filePath(QLatin1String(name));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
        return QString();
    }
    return path;
}

static QByteArray scenario(uint rate, nframes_t frames, const char* channels, const char* faults = "[]", int version = 1)
{
    return QStringLiteral(R"(# tscenario %1
{"rate": %2, "frames": %3, "channels": %4, "faults": %5})")
        .arg(version)
        .arg(rate)
        .arg(frames)
        .arg(QLatin1String(channels), QLatin1String(faults))
        .toUtf8();
}

// All frames of a reader at the file's rate, per channel, in reads of 4096
static std::vector<std::vector<float>> read_all(TAudioReader& reader)
{
    const uint channels = reader.channel_count();
    const nframes_t frames = reader.file_frame_count();
    std::vector<std::vector<float>> samples(channels, std::vector<float>(frames));
    TAudioBuffer buffer;
    for (nframes_t at = 0; at < frames; at += 4096) {
        const nframes_t count = std::min<nframes_t>(4096, frames - at);
        std::ignore = reader.read_file_frames(buffer, at, count);
        for (uint channel = 0; channel < channels; ++channel) {
            const float* data = buffer.get_channel_buffer(channel).get_data(count);
            std::copy(data, data + count, samples[channel].begin() + at);
        }
    }
    return samples;
}

static void test_signals(const QTemporaryDir& dir)
{
    const uint rate = 48000;
    const nframes_t frames = 2 * rate + 17;
    const QString path = write_file(dir, "signals.tscenario",
                                    scenario(rate, frames,
                                             R"([{"signal": "silence"},
                                                 {"signal": "sine", "frequency": 997, "amplitude": 0.5},
                                                 {"signal": "impulse", "frame": 12345, "amplitude": 0.8},
                                                 {"signal": "ramp"},
                                                 {"signal": "sweep", "mode": "exponential", "start": 20, "end": 20000, "amplitude": 0.7},
                                                 {"signal": "sweep", "mode": "linear", "start": 3000, "end": 300, "amplitude": 0.3}])"));
    check(TAudioReader::can_read(path), "a scenario can be read");
    const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
    check(reader != nullptr, "a scenario opens");
    if (!reader) {
        return;
    }
    check(reader->decoder_type() == QLatin1String("scenario"), "the scenario decoder reads it");
    check(reader->channel_count() == 6 && reader->file_rate() == rate && reader->file_frame_count() == frames, "its channels, rate and frames");

    const std::vector<std::function<float(nframes_t)>> references = {
        [](nframes_t frame) { return TestSignals::silence(frame); },
        [](nframes_t frame) { return TestSignals::sine(frame, rate, 997, 0.5); },
        [](nframes_t frame) { return TestSignals::impulse(frame, 12345, 0.8); },
        [](nframes_t frame) { return TestSignals::ramp(frame); },
        [](nframes_t frame) { return TestSignals::sweep_exponential(frame, rate, frames, 20, 20000, 0.7); },
        [](nframes_t frame) { return TestSignals::sweep_linear(frame, rate, frames, 3000, 300, 0.3); },
    };
    const char* names[] = {"silence", "sine", "impulse", "ramp", "exponential sweep", "linear sweep"};
    const std::vector<std::vector<float>> samples = read_all(*reader);
    for (uint channel = 0; channel < 6; ++channel) {
        float worst = 0.0f;
        for (nframes_t frame = 0; frame < frames; ++frame) {
            worst = std::max(worst, std::fabs(samples[channel][frame] - references[channel](frame)));
        }
        char what[120];
        std::snprintf(what, sizeof(what), "the %s as its reference (worst %.2g)", names[channel], double(worst));
        check(worst <= 1e-6f, what);
    }
}

// Q5: the ramp exact to the frame, the high part in a second channel past
// 2^24 frames
static void test_ramp(const QTemporaryDir& dir)
{
    const nframes_t frames = nframes_t(TestSignals::RampFrames) + 4096;
    const QString path = write_file(dir, "ramp.tscenario", scenario(48000, frames, R"([{"signal": "ramp"}, {"signal": "ramp-high"}])"));
    const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
    if (!reader) {
        check(false, "the ramp opens");
        return;
    }
    // Block by block: all of it at once would be 134 MB
    bool exact = true;
    TAudioBuffer buffer;
    for (nframes_t at = 0; at < frames; at += 65536) {
        const nframes_t count = std::min<nframes_t>(65536, frames - at);
        std::ignore = reader->read_file_frames(buffer, at, count);
        const float* low = buffer.get_channel_buffer(0).get_data(count);
        const float* high = buffer.get_channel_buffer(1).get_data(count);
        for (nframes_t i = 0; i < count; ++i) {
            const double value = double(low[i]) * TestSignals::RampFrames + double(high[i]) * TestSignals::RampFrames * TestSignals::RampFrames;
            exact = exact && value == double(at + i);
        }
    }
    check(exact, "the ramp gives every frame exactly, past 2^24 frames with its high part");
}

static void test_refused(const QTemporaryDir& dir)
{
    const char* sine = R"([{"signal": "sine", "frequency": 440, "amplitude": 0.5}])";

    const QString version = write_file(dir, "version.tscenario", scenario(48000, 1000, sine, "[]", 2));
    check(!TAudioReader::can_read(version) && !TAudioReader::open(version), "an unknown version does not open");

    const QString empty = write_file(dir, "empty.tscenario", scenario(48000, 0, sine));
    check(!TAudioReader::open(empty), "a scenario of no frames is refused");

    const QString broken = write_file(dir, "broken.tscenario", QByteArray("# tscenario 1\n{\"rate\": 48000,"));
    check(!TAudioReader::can_read(broken) && !TAudioReader::open(broken), "a scenario that does not parse is no audio file");

    const QString unknown = write_file(dir, "unknown.tscenario", scenario(48000, 1000, R"([{"signal": "noise"}])"));
    check(!TAudioReader::can_read(unknown) && !TAudioReader::open(unknown), "an unknown signal does not open");

    const QString fault = write_file(dir, "fault.tscenario", scenario(48000, 1000, sine, R"([{"kind": "read-error", "frame": 1000}])"));
    check(!TAudioReader::can_read(fault) && !TAudioReader::open(fault), "a fault outside the file does not open");

    // A WAV file named .tscenario is not passed on to the libraries
    const QString wav = write_file(dir, "riff.tscenario", QByteArray("RIFF\x24\0\0\0WAVEfmt ", 16));
    check(!TAudioReader::can_read(wav), "a file named .tscenario is a scenario or nothing");
}

// Q9: one frame, rates that do and do not divide the universal rate, mono
// and many channels
static void test_edges(const QTemporaryDir& dir)
{
    for (const uint rate : {8000u, 37800u, 44100u, 50000u, 192000u}) {
        const nframes_t frames = 3 * rate + 1;
        char name[40];
        std::snprintf(name, sizeof(name), "rate%u.tscenario", rate);
        const QString path = write_file(dir, name, scenario(rate, frames, R"([{"signal": "ramp"}])"));
        const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
        // The length as time: exact where the rate divides the universal
        // rate, else rounded up by less than one universal unit (TTimeRef's
        // frame constructor), so the frame at or before it is the frame count
        bool ok = reader && reader->file_rate() == rate && reader->file_frame_count() == frames
                  && TTimeRef::to_nframes(TTimeRef::frame_at_or_before(reader->length(), rate)) == frames;
        if (ok) {
            const std::vector<std::vector<float>> samples = read_all(*reader);
            ok = samples[0][frames - 1] == TestSignals::ramp(frames - 1);
        }
        char what[120];
        std::snprintf(what, sizeof(what), "at %u Hz: rate, frames, length and the last frame", rate);
        check(ok, what);
    }

    const QString single = write_file(dir, "single.tscenario", scenario(48000, 1, R"([{"signal": "impulse", "frame": 0, "amplitude": 0.25}])"));
    const std::unique_ptr<TAudioReader> one = TAudioReader::open(single).value_or(nullptr);
    bool ok = one && one->file_frame_count() == 1;
    if (ok) {
        TAudioBuffer buffer;
        const nframes_t read = one->read_file_frames(buffer, 0, 10).frames;
        const float* data = buffer.get_channel_buffer(0).get_data(10);
        ok = read == 1 && data[0] == 0.25f && std::all_of(data + 1, data + 10, [](float sample) { return sample == 0.0f; });
    }
    check(ok, "a single frame, then zeros");

    QByteArray many = "[";
    for (int channel = 0; channel < 32; ++channel) {
        many += QStringLiteral(R"(%1{"signal": "sine", "frequency": %2, "amplitude": 0.5})").arg(channel ? QStringLiteral(",") : QString()).arg(100 * (channel + 1)).toUtf8();
    }
    many += "]";
    const QString manyPath = write_file(dir, "many.tscenario", scenario(48000, 48000, many.constData()));
    const std::unique_ptr<TAudioReader> wide = TAudioReader::open(manyPath).value_or(nullptr);
    ok = wide && wide->channel_count() == 32;
    if (ok) {
        const std::vector<std::vector<float>> samples = read_all(*wide);
        for (uint channel = 0; channel < 32; ++channel) {
            ok = ok && samples[channel][1000] == TestSignals::sine(1000, 48000, 100.0 * (channel + 1), 0.5);
        }
    }
    check(ok, "32 channels, each its own signal");
}

// Q5: the ramp through the real writer as a float WAV and back through the
// real reader, exactly
static void test_ramp_through_writer(const QTemporaryDir& dir)
{
    const nframes_t frames = 100000;
    const QString path = write_file(dir, "write.tscenario", scenario(48000, frames, R"([{"signal": "ramp"}])"));
    const std::unique_ptr<TAudioReader> reader = TAudioReader::open(path).value_or(nullptr);
    if (!reader) {
        check(false, "the ramp to write opens");
        return;
    }
    TAudioBuffer buffer;
    std::ignore = reader->read_file_frames(buffer, 0, frames);

    TAudioEncodeSettings settings;
    settings.writerType = TraversoDAW::WriterType::SNDFILE;
    settings.fileFormat = TraversoDAW::FileFormat::WAV;
    settings.dataFormat = TraversoDAW::DataFormat::FLOAT;
    settings.channelCount = 1;
    settings.sampleRate = 48000;
    const QString wavPath = dir.filePath(QStringLiteral("ramp.wav"));
    const std::unique_ptr<TAudioWriter> writer = TAudioWriter::open(settings, wavPath, 48000).value_or(nullptr);
    // close() is required, also when the write failed
    const bool writtenAll = writer && writer->write(buffer, frames).frames == frames;
    const bool closed = writer && writer->close();
    const bool written = writtenAll && closed;
    check(written, "the ramp written as a float WAV");
    if (!written) {
        return;
    }

    const std::unique_ptr<TAudioReader> back = TAudioReader::open(wavPath).value_or(nullptr);
    bool exact = back && back->file_frame_count() == frames;
    if (exact) {
        const std::vector<std::vector<float>> samples = read_all(*back);
        for (nframes_t frame = 0; frame < frames; ++frame) {
            exact = exact && samples[0][frame] == TestSignals::ramp(frame);
        }
    }
    check(exact, "the float WAV reads back every frame of the ramp exactly");
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }

    test_signals(dir);
    test_ramp(dir);
    test_refused(dir);
    test_edges(dir);
    test_ramp_through_writer(dir);

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all scenario decoder tests passed\n");
    return 0;
}
