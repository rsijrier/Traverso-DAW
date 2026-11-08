// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TPeakBuildService.h"
#include "TObjectIdTags.h"
#include "TPeakFile.h"
#include "TTimeRef.h"
#include "TWaveformColumns.h"

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class TBufferedAudioStream;
class TBufferedAudioStreamReader;
class TPeakStream;
class TAudioBuffer;

/**
 * @brief The waveform of one audio source for its views.
 *
 * Opens the source's peak file (TPeakFile), has a missing or stale one
 * built (through the request it was given: the sheet's DiskIO builds in
 * its idle time), and answers column queries (TWaveformColumns) from the
 * pyramid or from the source's samples with a reader of its own. GUI
 * thread only.
 */
class TWaveform : public QObject, public TWaveformSampleSource
{
    Q_OBJECT

public:
    enum class State
    {
        Building,
        Ready,
        Failed
    };

    // source: the clip's reader. The waveform reads samples with a reader
    // of its own, opened on the first sample query and closed again when
    // unused for a while, so a clip normally holds one open file.
    // How the peak file gets built: starts or joins the job
    // frames: the source's, at its own rate (the build's weight among the
    // idle work)
    using RequestBuild = std::function<TPeakBuild(const QString& audioPath, const QString& peakPath, uint rate, int channelCount,
                                                  qint64 frames)>;
    TWaveform(TBufferedAudioStreamReader* source, RequestBuild build);
    // A recording in progress: painted from level 0 of its peak file as it
    // is written, no samples
    explicit TWaveform(std::shared_ptr<TPeakStream> live);
    // Where the peak file of an audio source lives
    static QString peak_path(const TBufferedAudioStream* source);
    ~TWaveform();

    // How far the waveform of a recording in progress reaches: the peak
    // blocks a read has now (the clip's length runs ahead of it)
    TTimeRef recorded_length() const;

    // Opens the peak file, or starts building it when it is missing or
    // stale. Called when the clip has its source and its sheet, so a peak
    // file never waits for its clip to become visible.
    void prepare();
    State state();
    int progress() const { return m_progress; }
    int channel_count() const;

    // One column per device pixel for the source time range starting at
    // start, see TWaveformColumns::device_columns(): from the power-of-two
    // grid from one frame per pixel on, exact where a grid column reaches
    // outside the clip's source range [clipStart, clipEnd). While Building:
    // from the peaks built so far, empty columns past them. Source::None
    // while Failed.
    TWaveformColumns::Source device_columns(const TTimeRef& start, const TTimeRef& columnWidth, int count,
                                            const TTimeRef& clipStart, const TTimeRef& clipEnd, TWaveformColumn* out);

    // The highest |sample| of all channels between start and end (source
    // time), at most 0.006 dB above it (TWaveformColumns::max_amplitude());
    // -1 while not Ready
    float max_amplitude(const TTimeRef& start, const TTimeRef& end);

    // The zero crossing of the source nearest to time, at most maxDistance
    // away; an invalid TTimeRef when there is none (or while not Ready)
    TTimeRef nearest_zero_crossing(const TTimeRef& time, const TTimeRef& maxDistance);

    // TWaveformSampleSource
    qint64 read(qint64 firstFrame, qint64 count, float* const* channels) override;

signals:
    void progress(int percent);
    // A build finished
    void ready();

private:
    TPeakFile::SourceInfo source_info() const;
    void open_or_build();
    void build_finished();
    void start_build(const char* reason);
    TBufferedAudioStreamReader* reader();

    AudioSourceId m_sourceId;
    QString m_fileName;
    uint m_rate{0};
    int m_channelCount{0};
    qint64 m_frameCount{0};
    bool m_sourceValid{false};
    TBufferedAudioStreamReader* m_source{nullptr};   // only while samples are read
    QTimer m_closeReader;
    std::shared_ptr<TPeakStream> m_live;
    // The peaks of the running build, until its file opens
    std::shared_ptr<TPeakStream> m_building;
    QString m_peakPath;
    TPeakFile m_file;
    std::unique_ptr<TAudioBuffer> m_decodeBuffer;
    RequestBuild m_requestBuild;
    QFutureWatcher<TPeakBuildResult> m_build;
    State m_state{State::Building};
    bool m_opened{false};
    int m_progress{0};
    int m_builds{0};
    int m_retries{0};
};
