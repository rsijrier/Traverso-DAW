// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TScenarioDecoder.h"

#include "TAudioBuffer.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>

static constexpr int ScenarioVersion = 1;
static constexpr QByteArrayView ScenarioLine("# tscenario ");
// A scenario is a short description (a few hundred bytes, some KB with
// many channels or faults); a larger file named .tscenario is not read whole
static constexpr qint64 MaxScenarioBytes = 64 * 1024;
static constexpr double RampFrames = 16777216.0;

// A whole number of frames, or nothing
static std::optional<nframes_t> frames_of(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (number < 0.0 || number > double(std::numeric_limits<nframes_t>::max()) || number != std::floor(number)) {
        return std::nullopt;
    }
    return nframes_t(number);
}

static std::optional<double> number_of(const QJsonObject& object, const char* key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        return std::nullopt;
    }
    return value.toDouble();
}

bool TScenarioDecoder::is_scenario_file(const QString& fileName)
{
    return fileName.endsWith(QLatin1String(".tscenario"), Qt::CaseInsensitive);
}

std::expected<void, QString> TScenarioDecoder::check(TByteSource& source)
{
    if (std::expected<Scenario, QString> scenario = parse(source); !scenario) {
        return std::unexpected(scenario.error());
    }
    return {};
}

std::expected<TScenarioDecoder::Scenario, QString> TScenarioDecoder::parse(TByteSource& source)
{
    const auto fail = [](const QString& reason) -> std::expected<Scenario, QString> {
        return std::unexpected(reason);
    };

    if (source.size() > MaxScenarioBytes) {
        return fail(QStringLiteral("too large for a scenario"));
    }
    QByteArray bytes(source.size(), '\0');
    if (!source.seek(0) || source.read(bytes.data(), bytes.size()) != bytes.size()) {
        return fail(QStringLiteral("cannot read it"));
    }
    // The first line names the format and its version, before anything parses
    const qsizetype lineEnd = bytes.indexOf('\n');
    QByteArray line = bytes.left(lineEnd);
    if (line.endsWith('\r')) {
        line.chop(1);
    }
    const QByteArray versionText = line.mid(ScenarioLine.size());
    const bool digits = !versionText.isEmpty() && std::all_of(versionText.begin(), versionText.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (lineEnd < 0 || !line.startsWith(ScenarioLine) || !digits) {
        return fail(QStringLiteral("no tscenario line"));
    }
    if (versionText.toInt() != ScenarioVersion) {
        return fail(QStringLiteral("tscenario version %1, this Traverso reads %2").arg(QString::fromLatin1(versionText)).arg(ScenarioVersion));
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes.mid(lineEnd + 1), &error);
    if (!document.isObject()) {
        return fail(QStringLiteral("no JSON object (%1)").arg(error.errorString()));
    }
    const QJsonObject root = document.object();

    Scenario scenario;
    const std::optional<nframes_t> rate = frames_of(root.value(QLatin1String("rate")));
    const std::optional<nframes_t> frames = frames_of(root.value(QLatin1String("frames")));
    if (!rate || *rate == 0 || !frames) {
        return fail(QStringLiteral("rate or frames missing or not a whole number"));
    }
    scenario.rate = *rate;
    scenario.frames = *frames;

    const QJsonArray channels = root.value(QLatin1String("channels")).toArray();
    if (channels.isEmpty()) {
        return fail(QStringLiteral("no channels"));
    }
    for (const QJsonValue& value : channels) {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QLatin1String("signal")).toString();
        Signal signal;
        if (name == QLatin1String("silence")) {
            signal.kind = Signal::Kind::Silence;
        } else if (name == QLatin1String("ramp")) {
            signal.kind = Signal::Kind::Ramp;
        } else if (name == QLatin1String("ramp-high")) {
            signal.kind = Signal::Kind::RampHigh;
        } else if (name == QLatin1String("sine")) {
            const std::optional<double> frequency = number_of(object, "frequency");
            const std::optional<double> amplitude = number_of(object, "amplitude");
            if (!frequency || !amplitude) {
                return fail(QStringLiteral("a sine needs frequency and amplitude"));
            }
            signal.kind = Signal::Kind::Sine;
            signal.frequency = *frequency;
            signal.amplitude = *amplitude;
        } else if (name == QLatin1String("impulse")) {
            const std::optional<nframes_t> frame = frames_of(object.value(QLatin1String("frame")));
            const std::optional<double> amplitude = number_of(object, "amplitude");
            if (!frame || !amplitude) {
                return fail(QStringLiteral("an impulse needs frame and amplitude"));
            }
            if (*frame >= scenario.frames) {
                return fail(QStringLiteral("an impulse's frame is inside the file"));
            }
            signal.kind = Signal::Kind::Impulse;
            signal.frame = *frame;
            signal.amplitude = *amplitude;
        } else if (name == QLatin1String("sweep")) {
            const QString mode = object.value(QLatin1String("mode")).toString();
            const std::optional<double> start = number_of(object, "start");
            const std::optional<double> end = number_of(object, "end");
            const std::optional<double> amplitude = number_of(object, "amplitude");
            if (!start || !end || !amplitude || *start <= 0.0 || *end <= 0.0) {
                return fail(QStringLiteral("a sweep needs start and end above 0 Hz and amplitude"));
            }
            if (mode == QLatin1String("exponential")) {
                // From a frequency to itself the rate of change is 0, and
                // its phase divides by it: a NaN no fault asked for
                if (*start == *end) {
                    return fail(QStringLiteral("an exponential sweep needs start and end apart (a sine is one frequency)"));
                }
                signal.kind = Signal::Kind::SweepExponential;
            } else if (mode == QLatin1String("linear")) {
                signal.kind = Signal::Kind::SweepLinear;
            } else {
                return fail(QStringLiteral("a sweep's mode is exponential or linear"));
            }
            signal.start = *start;
            signal.end = *end;
            signal.amplitude = *amplitude;
        } else {
            return fail(QStringLiteral("unknown signal \"%1\"").arg(name));
        }
        scenario.channels.push_back(signal);
    }

    const QJsonValue faults = root.value(QLatin1String("faults"));
    if (!faults.isUndefined() && !faults.isArray()) {
        return fail(QStringLiteral("faults is a list"));
    }
    for (const QJsonValue& value : faults.toArray()) {
        const QJsonObject object = value.toObject();
        const QString kind = object.value(QLatin1String("kind")).toString();
        const std::optional<nframes_t> frame = frames_of(object.value(QLatin1String("frame")));
        if (!frame || *frame >= scenario.frames) {
            return fail(QStringLiteral("a fault needs a frame inside the file"));
        }
        Fault fault;
        fault.first = *frame;
        // Without frames: to the end of the file
        fault.frames = scenario.frames - *frame;
        if (kind != QLatin1String("end") && object.contains(QLatin1String("frames"))) {
            const std::optional<nframes_t> frames = frames_of(object.value(QLatin1String("frames")));
            if (!frames || *frames == 0) {
                return fail(QStringLiteral("a fault's frames is a whole number above 0"));
            }
            fault.frames = std::min(*frames, fault.frames);
        }
        if (kind == QLatin1String("read-error")) {
            fault.kind = Fault::Kind::ReadError;
        } else if (kind == QLatin1String("end")) {
            // The audio ends here: no frame after it reads
            fault.kind = Fault::Kind::ReadError;
            fault.frames = scenario.frames - *frame;
        } else if (kind == QLatin1String("seek-error")) {
            fault.kind = Fault::Kind::SeekError;
        } else if (kind == QLatin1String("value")) {
            const QString which = object.value(QLatin1String("value")).toString();
            const double channel = object.value(QLatin1String("channel")).toDouble(-1.0);
            if (channel < 0.0 || channel >= double(scenario.channels.size()) || channel != std::floor(channel)) {
                return fail(QStringLiteral("a value fault needs a channel of the file"));
            }
            fault.kind = Fault::Kind::Value;
            fault.frames = 1;
            fault.channel = uint(channel);
            if (which == QLatin1String("nan")) {
                fault.value = std::numeric_limits<float>::quiet_NaN();
            } else if (which == QLatin1String("inf")) {
                fault.value = std::numeric_limits<float>::infinity();
            } else if (which == QLatin1String("-inf")) {
                fault.value = -std::numeric_limits<float>::infinity();
            } else {
                return fail(QStringLiteral("a value fault's value is nan, inf or -inf"));
            }
        } else {
            return fail(QStringLiteral("unknown fault \"%1\"").arg(kind));
        }
        scenario.faults.push_back(fault);
    }

    return scenario;
}

TScenarioDecoder::TScenarioDecoder(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
    : TAudioDecoder(fileName, std::move(source), opener)
{
}

std::expected<std::unique_ptr<TScenarioDecoder>, TAudioFileError> TScenarioDecoder::open(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
{
    return TAudioDecoderFactory::open<TScenarioDecoder>(fileName, std::move(source), opener);
}

TAudioFileStatus TScenarioDecoder::start()
{
    std::expected<Scenario, QString> scenario = parse(source());
    if (!scenario) {
        return std::unexpected(TAudioFileError{TAudioFileError::Code::CannotOpen, QStringLiteral("not a scenario: %1").arg(scenario.error())});
    }
    m_rate = scenario->rate;
    m_frames = scenario->frames;
    m_channels = uint(scenario->channels.size());
    m_signals = std::move(scenario->channels);
    m_faults = std::move(scenario->faults);
    return has_audio();
}

float TScenarioDecoder::sample(const Signal& signal, nframes_t frame) const
{
    // In double, as the tests' reference (tests/common/test_signals.h)
    // computes them, each on its own
    const double t = double(frame) / m_rate;
    const double length = double(m_frames) / m_rate;
    switch (signal.kind) {
    case Signal::Kind::Silence:
        return 0.0f;
    case Signal::Kind::Sine:
        return float(signal.amplitude * std::sin(2.0 * M_PI * signal.frequency * double(frame) / m_rate));
    case Signal::Kind::Impulse:
        return frame == signal.frame ? float(signal.amplitude) : 0.0f;
    case Signal::Kind::Ramp:
        return float(double(frame % nframes_t(RampFrames)) / RampFrames);
    case Signal::Kind::RampHigh:
        return float(double(frame / nframes_t(RampFrames)) / RampFrames);
    case Signal::Kind::SweepExponential: {
        const double k = std::log(signal.end / signal.start) / length;
        return float(signal.amplitude * std::sin(2.0 * M_PI * signal.start * (std::exp(k * t) - 1.0) / k));
    }
    case Signal::Kind::SweepLinear:
        return float(signal.amplitude * std::sin(2.0 * M_PI * (signal.start * t + (signal.end - signal.start) * t * t / (2.0 * length))));
    }
    return 0.0f;
}

TAudioFileStatus TScenarioDecoder::seek_frame(nframes_t frame)
{
    Q_ASSERT(frame < m_frames);
    for (const Fault& fault : m_faults) {
        if (fault.kind == Fault::Kind::SeekError && frame >= fault.first && frame - fault.first < fault.frames) {
            return seek_failed(frame, QStringLiteral("the scenario's seek-error at frame %1").arg(fault.first));
        }
    }
    return seek_landed(frame);
}

TAudioFileResult TScenarioDecoder::read_frames(TAudioBuffer& buffer, nframes_t count)
{
    if (count == 0) {
        return {};
    }
    buffer.check_capacity(count, m_channels);

    nframes_t readFrames = 0;
    if (!at_end() && !m_inErrorState) {
        // Up to the end of the file, and up to a damaged part: the frames
        // before it are read, then the error state (read_done())
        readFrames = std::min(count, m_frames - m_nextFrame);
        for (const Fault& fault : m_faults) {
            if (fault.kind == Fault::Kind::ReadError && fault.first < m_nextFrame + readFrames && fault.first + fault.frames > m_nextFrame) {
                readFrames = fault.first > m_nextFrame ? std::min(readFrames, fault.first - m_nextFrame) : 0;
            }
        }
        for (uint channel = 0; channel < m_channels; ++channel) {
            audio_sample_t* data = buffer.get_channel_buffer(channel).get_data(readFrames);
            for (nframes_t i = 0; i < readFrames; ++i) {
                data[i] = sample(m_signals[channel], m_nextFrame + i);
            }
        }
        for (const Fault& fault : m_faults) {
            if (fault.kind == Fault::Kind::Value && fault.first >= m_nextFrame && fault.first < m_nextFrame + readFrames) {
                buffer.get_channel_buffer(fault.channel).get_data(readFrames)[fault.first - m_nextFrame] = fault.value;
            }
        }
    }
    return read_done(buffer, readFrames, count, readFrames < count ? QStringLiteral("the scenario's damaged part or end") : QString());
}
