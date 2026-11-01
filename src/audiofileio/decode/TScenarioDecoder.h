// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TAudioDecoder.h"

#include <expected>
#include <optional>
#include <vector>

/**
 * @brief The decoder of a scenario file (.tscenario): a description of a
 * signal instead of its samples, for headless tests of what reads audio
 * files. It keeps the TAudioDecoder contract like every decoder and is
 * chosen by the factory like them, by the file's content.
 *
 * The first line is exactly "# tscenario <version>", ended by \n or \r\n; the
 * version says what follows. In version 1 that is JSON:
 *
 *     # tscenario 1
 *     {
 *       "rate": 48000,
 *       "frames": 96000,
 *       "channels": [
 *         {"signal": "sine", "frequency": 440, "amplitude": 0.5},
 *         {"signal": "ramp"}
 *       ],
 *       "faults": [
 *         {"kind": "read-error", "frame": 1000, "frames": 500}
 *       ]
 *     }
 *
 * Signals, one per channel: "silence"; "sine" (frequency, amplitude);
 * "impulse" (frame, amplitude); "ramp" (the frame / 2^24, exact in float up
 * to 2^24 frames, then it starts again: its low part) and "ramp-high" (the
 * frame's high part, (frame / 2^24) / 2^24): the two channels together give
 * every frame of longer material exactly; "sweep" over the whole length
 * (mode "exponential" with start and end apart, or "linear"; start, end,
 * amplitude).
 *
 * Faults, only when the file asks for them, and only what a real file or
 * decoder can do (the contract): "read-error" (frame, frames: a damaged part,
 * to the end without frames): a read gives the frames before it, then the
 * error state; "seek-error" (frame, frames): a seek into that part fails;
 * "end" (frame): the audio ends there, before the frame count (a header that
 * says more); as a WAV file does it: a seek into the ended part succeeds and
 * the read there fails (another format may refuse such a seek instead);
 * "value" (frame, channel, value "nan", "inf" or "-inf").
 *
 * Another version does not open, and it is known before anything parses. A
 * file that does not parse is no scenario: no decoder reads it.
 */
class TScenarioDecoder : public TAudioDecoder
{
public:
    QString decoder_type() const override { return QStringLiteral("scenario"); }

    // The source parses as a scenario, or why not
    [[nodiscard]] static std::expected<void, QString> check(TByteSource& source);

    TAudioFileStatus seek_frame(nframes_t frame) override;
    TAudioFileResult read_frames(TAudioBuffer& buffer, nframes_t count) override;

private:
    friend struct TAudioDecoderFactory;

    TScenarioDecoder(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener);
    TAudioFileStatus start();

    struct Signal
    {
        enum class Kind { Silence, Sine, Impulse, Ramp, RampHigh, SweepExponential, SweepLinear };
        Kind kind{Kind::Silence};
        double frequency{0.0};
        double start{0.0};
        double end{0.0};
        double amplitude{0.0};
        nframes_t frame{0};
    };

    struct Fault
    {
        enum class Kind { ReadError, SeekError, Value };
        Kind kind{Kind::ReadError};
        // The part of the file: [first, first + frames)
        nframes_t first{0};
        nframes_t frames{0};
        uint channel{0};
        float value{0.0f};
    };

    struct Scenario
    {
        uint rate{0};
        nframes_t frames{0};
        std::vector<Signal> channels;
        std::vector<Fault> faults;
    };

    static std::expected<Scenario, QString> parse(TByteSource& source);
    float sample(const Signal& signal, nframes_t frame) const;

    std::vector<Signal> m_signals;
    std::vector<Fault> m_faults;
};
