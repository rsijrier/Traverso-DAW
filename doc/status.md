<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Traverso status

Where each foundation stands, what was verified how, and the open tasks by
priority. The agent updates this file at the end of every session; Remon
checks it when reviewing. It replaces the draft outside the repository
(`../status.md`, 2026-10-22) and the task list in the agent's notes.

First version 2026-10-27, made from git (branches, worktrees, log), the old
draft and the agent's notes. Lines marked **(check)** could not be verified
from the repository: Remon confirms or corrects them.

Rule: at most three areas "in progress" at the same time. A fourth one waits,
or another one is finished or parked first.

## Release planning

Set by Remon, 2026-10-30. Existing tasks keep their place in the list
below; this section only says which of them the release needs.

Timebox
- Until the end of November 2026: points 1 to 4 below.
- December 2026: finishing, tests and bugs. This month does not shrink
  when November runs late.
- Goal: a release around the turn of the year.

Release scope (must have)
1. Decouple the domains (among others the audio clip and the track from
   the song), only as far as the controllers and the audio runtime need it.
2. Controllers: an unambiguous cursor, zoom, scroll and selection on the
   canvas (branch 3, `input-controllers`; T39). The other commands may move to
   the new model later, as long as the old route works.
3. Audio runtime: only what freewheeling and bounce/export need (T34, T41;
   T58 is in progress on `device-ownership`). The room for more outputs
   (live playhead, `multi-transport.md`; "Decisions taken": a second
   output device) stays open, but is not built.
4. 64-bit file offsets (T71 step 2a, built on `audio-byte-source`): a
   precondition, because Debian builds on 32-bit ARM. Reviewed and merged
   together with step 1 and 2b (below).

Small finishing touches before the release
- The byte source, T71 steps 1, 2a and 2b (`audio-byte-source`, all
  built): review and merge together. Step 2b (seek generations) is seek
  correctness already built; leaving it while the branch ages is waste.
- A platform header (`TPlatform.h`): find hidden wrong code branches. New;
  the header does not exist yet (T71 step 2c names it).

After the release, pulled by a use case
- Byte source steps 2c (a read pool per medium, a decode pool,
  ioscheduler free of Qt) and 2d (the window in time). Trigger: the live
  playhead, libraries on a NAS or HDD, or a measured seek problem. (T71;
  T61.)
- A long file's peaks built in parallel, chunk by chunk (T19;
  `peak-records.md` section 7). Waits for the kept seek index (T71 step 4),
  the read pool per medium and the windows of byteio (T71 step 2c).
- The byte source as a library of its own, and the write direction.
- Waveform performance through shaders (`gpu-waveform-poc`,
  `curve-gpu-bench`; Foundations: organic interface).
- TTimeRef free of Qt (T12).
- The live playhead as a feature (`multi-transport.md`; T60).
- Clean-up: `interleaved_scratch()` in one place (T75).

## Legend

- **done**: tests green, design doc current, merged into master
- **in progress**: being worked on now
- **awaiting review**: built, Remon reviews before the merge
- **parked**: started, stopped on purpose; the note says where it stands
- **planned**: designed or decided, not started

## Repository

- `master`: pushed to `origin` 2026-11-02.
- Worktrees: `traverso` (master), `traverso-input-controllers`,
  `traverso-gpu-waveform-poc`, `traverso-video-poc`. The branch `curve-gpu-bench`
  is kept without a worktree. `gpu-waveform-poc` and `video-poc` stay local, a hook
  refuses to push them.

## Foundations

| Area | Status | Branch | Design doc | Next step |
|---|---|---|---|---|
| Library split (support, dsp, audioruntime, audiofileio, waveform, ioscheduler) | done | master | — | T15: each lib and a headless test on its own |
| RT state (TRtList, TRtValue, TRtTransaction, curves, fades, owners) | done | master (2026-10-14…17) | `rt-transaction-design` (agent notes; not in the repo) **(check)** | processing-node graph design (not now); JACK port lists with the JACK revision |
| DSP: buffer operations (portable and AVX2), `TGain`, `TFaderLaw`, `TDenormals`, `TCpuFeatures` | done | master (T52, T51 `28216d8e`; T20 `9ab838d7`) | `t20-buffer-operations-measurement.md` | denormals on ARM (FPCR.FZ, later); T15 |
| Audio runtime: `TAudioDevice` races, ownership, the singletons | T58 step 2 done | master (`73dad650`, `02b39fcc`) | — | T59–T61; for the release T34, T41 |
| Session log (TLog) | done | master (2026-10-17) | — | — |
| GUI frame clock | done | master (2026-10-17) | — | — |
| Audio file I/O: `TAudioReader` / `TAudioWriter`, decoders sample exact, resampler choice; MP3 on libmpg123, AAC and MP4 on FFmpeg | done | master (2026-11-02) | `mp3-decoder.md` | T8, T10, T12, T21, T22, T26, T27, T86, T88, T89, T91; FAAD waits for Ben |
| Byte source, 64-bit offsets (T71 steps 1, 2a, 2b) | done | master (`d916d50b`) | | steps 2c and 2d after the release |
| Waveform library, peak format v4, waveform grid | done | master (review stack, 2026-10-25) | `peak-records.md`, `waveform-grid.md` | post-merge items (below); Remon's in-app grid check **(check)** |
| IO scheduler (lib, thread, app-wide schedulers, dispatcher) | done | master (review stack, 2026-10-25) | `diskio-app-wide.md`, `diskio-rounds.md` | T4 (stream-window race); requirements R1–R18 of `diskio-app-wide.md` §0 still marked concept **(check)** |
| Recording latency compensation | done, not runtime-tested with a loopback | master (review stack) | `recording-latency.md` | loopback test |
| Recording session (`TRecordingSession`) | done | master (review stack) | `recording-session.md` | writer ownership by the session (`ioscheduler-taak.md` "Later") **(check)** |
| Real-time path (markers, `TRealTimeClosure` / `TGuiClosure`, RTSan, mlock) | done (reviewed 2026-10-25; the commits after that review, incl. f7a782d6 and 654144c6 of 2026-10-27, not yet seen by Remon; and `run_cycle()` changed again on 2026-10-28: T51 denormals, T53 races, T54 storm flag, not yet checked against the note) | master (2026-10-27, `654144c6`) | `doc/design/realtime-audio-path.md` | Clang: 7 `-Wfunction-effects` reports left (split `transport_control()`: 3; JACK port lists: 4), then step 5 (`-Werror=function-effects`) and 6 of the note |
| Headless core tests: scenario decoder | steps 1–4 done (reviewed by Remon 2026-10-27, merged `184eca11`); also peak builds through the reader tested | master | `Work order fake decoder for headless core tests (revised).md` (outside the repo; status note and "Step 3 check" inside) | step 5 waits for O1 (wanted behaviour per fault); steps 6–8 wait for T6, `TTestProject`, a scheduler seam |
| Controllers / input layer (branch 3) | awaiting answers | `input-controllers` (design only, 2026-10-18) | `input-controllers.md` (on the branch), `actions-and-controllers.md` (outside) | Remon answers §10 (11 questions; 1, 2, 3, 5, 6, 8 block the fader reference port); the fader port takes `TFaderLaw` (the position mapping, dsp since T52) along to the controllers, headless tested there (2026-10-28) |
| Shortcut system | first part done (action registry, 2026-10-14) | master | — | the rest after branch 3 (explicit items, marker classes, hold-tool factories, QML) |
| Test fixture (`TTestProject`) | planned | — | `actions-and-controllers.md` §6 | own task, after T6 |
| TimeRef enforcement and timebase | planned; interim fix done (`aaa94dbc`) | — | `design-principles.md` §5 (outside) | T12 first; universal rate 112,896,000 and the upgrade path for old projects before a release |
| Organic interface (Qt Quick timeline) | planned | `curve-gpu-bench` (reference only) | `curve-gpu-benchmark.md` (on that branch) | after controllers and the fixture |

Two areas are in progress: the scenario decoder and branch 3, both waiting for
answers.

## Review of 2026-10-25, per branch

How each branch of the review stack and `realtime-path` was verified (from the
retrospective of 2026-10-26).

| # | Branch (commits) | Time | How verified | Found | Result |
|---|---|---|---|---|---|
| 1 | realtime-path (11) | 15 min | runtime test (Minidisk: play, stop, seek, move, split, fade), the markers in Qt Creator, §7 of the note | marker placement (rejected after explanation), the closure fault, TLB/kernel fences | §5 extended; that evening the renames and all §7 decisions |
| 2 | the agent's fix commits on the stack (±35) | 50 min | line by line | double `to_nframes()`, `write()` vs `planar_write`, libsndfile types in core, `multiply()` used once, `write_buffer` double work, `friend` and a private `m_size` | 5 commits (2f218074, 0581685a, 06cb3110, d31e5049, 26a780f8), a task, "buffers on the path" |
| 3 | resampler-choice (4) | 45 min, kdiff3 | dependencies for headless tests | settings read in the stream reader, `config()` below the app | `TResamplerSettings` (48d48c84, d0e0a1fa); T6 part |
| 4 | app-scheduler (23) | 12 min | read | no issues; two wake-ups per process cycle | note |
| 5 | recording-latency (15) | 12 min | read | member names `m_readSource` / `m_writer` | rename in T6 |
| 6 | recording-session (9) | 7 min | read, judged on risk | `TRecordingCommand` looks like a copy of `TCommand` | task with the controller refactor; accepted: removable on its own without effect on the pipeline |
| 7 | peak-format-v4 (8) | 16 min | runtime test by ear | nothing; an exact headless test would tell more | fast-forward |

Open from this review: the largest and most concurrent branch (app-scheduler)
got the least time; it is reviewed against the thread contract
(`diskio-rounds.md` §2) when the scheduler is touched next.

## Tasks, by priority

Every task has a fixed ID (T and a number) that never changes: code
comments, documents and commits refer to it. The order of the list is the
rank, with a priority label (high … very low); a new task gets the next free
number and goes where its rank is, nothing is renumbered. Code problems found
in a review come here, with facts and a proposal, and are not decided or
built during the review; Remon ranks them.

T1–T3 are done (2026-10-24: reader layering and names, writer finish, lossy
decoders sample exact). Until 2026-10-27 the tasks had R numbers that were
the rank and shifted on every insertion; on 2026-10-27 evening every Rn
became Tn with the same number (T4–T28), and older notes, commits and
documents keep their R numbers of the day. The unranked tasks below got
T29 onwards. (Not to confuse with the requirement numbers R1–R28 of
`diskio-app-wide.md`, which are that document's own.)

- **T4 (high): race in the stream windows.** The GUI writes `m_streamStart`,
  `m_streamEnd` and `m_sourceStartLocation` of a reader in place
  (`TAudioClip.cpp:805`, split/trim at `:263,344,356,414,761`) while the
  scheduler thread (`transport_in_window()`) and a seek step
  (`TBufferedAudioStreamReader.cpp:318-339,405`) read them. Only while editing
  during playback. Proposal (awaiting Remon): a `Window{start, end,
  sourceStart}` behind a mutex, set in one call, copied once per step; a TSan
  test. Describe first what happens to audio already in the ring. Checked in the
  code 2026-11-02: still so, `set_stream_location_window()` writes two plain
  `TTimeRef`s, `TAudioClip.cpp:807`.
- **T5 (high): imports and merges resampled at the device rate**
  (`TBufferedAudioStreamWriter.cpp:60`; Fable review 2026-10-25): with the
  device rate ≠ the source rate a converted import comes out at the wrong
  length and pitch. Runtime test first (New Project dialog converting a file,
  device rate ≠ source rate). Read in the code 2026-11-02, not run: the writer now
  gets the device rate, but `TAudioFileCopyConvert.cpp` reads `nframes` at the device
  rate and adds them to the location at the file's rate, so the location runs away
  from what was read when the two differ. `TAudioFileMerger.cpp:95` goes the same way.
- **T6 (medium-high): split `TBufferedAudioStreamReader` into the source
  record and the live stream.** Record always present per clip; `open(record)`
  gives a stream or nullptr; `m_error`, `init()`, `m_isReadSourceValid` go;
  relinking a missing file becomes possible; fixes the unchecked dereference
  in `AudioClipEditDialog.cpp:125`. Also: no `config()` below the app (the
  recording settings first; removes the link cycle that blocks core tests);
  renames `m_readSource` → `m_playbackStream`, `m_writer` →
  `m_recordingStream`, `m_readSourceId` → `m_sourceId`; with it the
  `TAudioWriter` close result and `TAudioFileMerger`'s missing
  `finish_file()` (review finding #18).
- **T7 (medium-high): a recording's written-frame count is in input
  frames.** `TBufferedAudioStreamWriter::m_writtenFrames` counts frames at
  the device rate (`TBufferedAudioStreamWriter.cpp:169`), while
  `TWaveform.cpp:99` and `TPeakBuildService.cpp:104` read it as the file's
  frames at the file's rate: a wrong live-waveform length and peak build
  when the rates differ (Fable, review of the audiofileio API work order,
  2026-10-27). Corrected 2026-11-02: those two read `TPeakStream::written_frames()`,
  not the writer's count, whose only user is the empty-take check, and the live
  length is right: frames and rate are both the device's. What is left to check: a
  recording at another project rate gets its peaks at the device rate.
- **T8 (medium): NaN to the output.** Prevent at the reader boundary on the
  decoder's output (before the resampler); a safety net in
  `TAudioDevice::run_one_cycle()` before `m_driver->write()` (NaN also comes
  from plugins: a named exception to "prevent, do not detect"). Export is not
  covered by the net: a third place, or out of scope.
- **T9 (done 2026-10-27: reviewed by Remon, merged `aade2a79`): audiofileio errors** (`doc/design/audiofileio-errors.md`):
  one error type, `std::expected` for open, close and the rate, a result
  with the count and the error for every block call, decoders and encoders
  included, `close()` required (with it the merger's missing close,
  `TAudioFileMerger.cpp:121-124`), `TAudioResampler::valid()`. With it:
  `MadAudioReader` checks its `seek(0)` at open (`:587`), FAAD's dead
  `if (!d)` and its size narrowing (`:600`, `:256`). Before T10, which needs
  its error.
- **T10 (medium): read errors are hidden in core.** The reader reports them
  now (a short count on both paths, done in `scenario-decoder`); the stream
  logs or ignores the count and sets IN_SYNC
  (`TBufferedAudioStreamReader.cpp:381-384,465-469,501`), and the peak build
  stops at a damaged part (`TAudioFileBuildSource::read()` returning -1 on
  an error, as its contract says, is decided here, not in T9). The error
  reaches the stream through T9. Wanted behaviour first (open question O1,
  parked 2026-10-27): silence and one log line per stream, or a read-error
  state and a signal to the clip.
  Note (Remon's review of W1–W4, 2026-10-28): the same error comes back.
  The resampled path reads ahead, so a read gives all its frames plus the
  error met beyond them; the next read sees the decoder's error state,
  repositions, the seek succeeds (the damaged part lies further on), the
  error is cleared, and the read-ahead hits the same part again: at least
  two reports of one fault, each costing a seek (MP3: with pre-roll). Not
  wrong, but "one log line per stream" needs de-duplication by position,
  not by count.
- **T11 (dropped 2026-10-31 with T73): MP3 without libmad goes to libsndfile,
  whose seeks are not exact.** MP3 no longer goes through MAD or libsndfile
  (`doc/design/mp3-decoder.md`).
- **T12 (medium): TTimeRef frame-conversion cleanup.** One symmetric style
  (`from_frames` ↔ `frame_at_or_before/after`), one call for rate-to-rate
  counts, one call for `to_nframes(frame_*(...))` (8 sites). The text/QTime
  family moves to core in the Qt-free step below.
  Order (Remon, 2026-10-30): first the frame-conversion cleanup above, then
  Qt-free (the revision goal below), with the higher universal rate in the
  same opening, so TTimeRef is not broken open twice.
  Revision goal (Remon, 2026-10-30): TTimeRef becomes fully independent of
  Qt, a bare arithmetic type in support, plain C++. Everything that needs
  Qt moves to core.
  - In support (plain C++):
    - `qint64` to `int64_t`. Careful: `qint64` is always `long long`;
      `int64_t` is `long` on Linux x86-64 but `long long` on macOS and
      Windows, the same type as `qint64` there. Overloads on `qint64`,
      `int` and `uint64_t` that compile on Linux collide on macOS and
      Windows (and the reverse). The rule that solves it: one integer type
      in the API, no overloads on several integer types, the meaning in
      the name (the private raw constructor and named factories, e.g. one
      for universal units and one for frames at a rate). Not as a rename
      on its own.
    - `Q_ASSERT` out (9 `Q_ASSERT` + 3 `Q_ASSERT_X` in `TTimeRef.h`/`.cpp`,
      partly in `T_REALTIME_SAFE` functions), replaced by an assert macro
      of support's own.
    - No `QString`, `QLocale` or `QRegularExpression` in TTimeRef any more.
  - To core (the Qt side):
    - Text and parsing: `TTimeRef.cpp` from line 143 to the end
      (`timeref_to_*`, `*_to_timeref`) becomes a formatting helper of its
      own; outside TTimeRef 57 lines in 20 files use it (counted
      2026-10-30).
    - `Q_DECLARE_METATYPE(TTimeRef)`: now `TTimeRef.h:304`, used in
      `PCommand.cpp`, `TFadeRangeCommand.cpp` and `Traverso.cpp`.
    - Open: whether QML needs a value type for TTimeRef (`Q_GADGET`)
      depends on how much QML the timeline gets. Not designed now.
  - Gain: `TTimeRef.h` now pulls Qt headers into every library that uses
    it (among others through `TIOStream.h`); a Qt-free TTimeRef removes
    that.
  - With it, the timebase: the universal rate from 28,224,000 to
    112,896,000 for rates above 192 kHz, and the upgrade of old projects
    (by the project file converter, T42; no load code for old files). It
    touches the same raw constructor and factories (Foundations: "TimeRef
    enforcement and timebase"; T27; "Decisions waiting": the upgrade path
    before a release).
- **T13 (medium): clocks.** One function per domain: monotonic for intervals
  (driver cycles, scheduler, rings, autorepeat, profiling), wall time only
  where stored; stored times in UTC (backup file name, session log header).
  Also: `TTimeRef::get_*seconds_since_epoch()` use `high_resolution_clock`
  (the wall clock in libstdc++) for intervals, and return `long`, which is
  32 bits on Windows: nanoseconds since 1970 overflow there (`qint64` or
  `trav_time_t`).
  With it (Remon, 2026-10-28): the thread's own CPU time next to the
  monotonic clock, to see whether the CPU goes where it should: to
  Traverso's code, or to the system.
  - The DSP load stays elapsed time against the period (the deadline,
    what decides a dropout). The thread's CPU time is what Traverso's code
    costs; the difference is the system's share (preemption, interrupts,
    drivers, power management).
  - Linux and macOS: `CLOCK_THREAD_CPUTIME_ID`, per cycle, nanoseconds (a
    system call on Linux, not the vDSO: `T_REALTIME_SAFE_BY_CONTRACT`).
    This is the precise measurement, for development.
  - Windows: no per-cycle time (`GetThreadTimes` counts in scheduler ticks
    of about 15.6 ms; `QueryThreadCycleTime` gives CPU cycles, not time):
    only a share over the meter's interval, the thread's cycles against
    the cycles that passed. Enough to tell Traverso's share from the
    system's. What Traverso's code costs is about the same as on Linux
    (same code and, with MinGW, the same compiler and the AVX2 path); the
    system's share is not, and on Windows it is where the trouble usually
    is (drivers that hold a DPC too long, MMCSS priorities). A user with
    dropouts and a low Traverso share: point to LatencyMon.
- **T14 (medium): strength of the unique IDs.** `create_id()` = seconds × 10⁹
  + `rand()` seeded by seconds; collisions possible across seconds and between
  two instances. Investigate where uniqueness is assumed, then a stronger
  scheme (format change through the project converter).
- **T15 (medium): each lib and a headless test on its own.** File format
  enums out of `support/defines.h` into audiofileio; the encode settings
  without dsp headers; the app without ALSA and lilv headers.
  With it (API review I2, 2026-10-27): `TAudioEncodeSettings` gets one format
  field: `M4A` in `FileFormat`, `create_audio_encoder()` chooses by
  `fileFormat`, `writerType` and the dead `blockSize` leave the settings
  (`writerType`/`fileFormat` must agree today without a check).
  With it, `support/defines.h` goes once the enums are out. Done before
  (2026-10-28, `a9091f38`, `e47c4deb`): the basic types in `TTypes.h`
  (`trav_time_t` as `std::int64_t`, no Qt), the rest of `defines.h`
  removed (relaytool, the Windows shim, `PortFlags`, profiling), every
  other include switched; four files still include `defines.h` for the
  enums: `TExportSpecification.h`, `TAudioEncodeSettings.h`,
  `TAudioFileCopyConvert.cpp`, `TAudioFileMerger.cpp`.
- **T16 (medium): `transport_position` test failed 3 times under ctest load**
  (2026-10-27, Clang and GCC; not reproduced in 72 parallel runs). First find
  out which check: a torn read is a real race in the frame clock's seqlock, a
  starved reader only a test too tight for a loaded machine. Seen again on
  2026-10-31: "the reader is not starved by the writer", under `ctest -j4` and
  once alone; 30 runs alone after that all passed. Was T74 for a moment.
- **T17 (medium-low): a WavPack file of unknown length** reports 2^32-1
  frames (`WavpackGetNumSamples()` -1, `WPAudioReader.cpp:45`): a clip of
  27 hours, and an error state at its real end.
- **T18 (done 2026-10-27 with T9): `WPAudioWriter::write_block()`** closes the file and
  nulls `m_wp` on a failed write without a log line, and
  `WavpackCloseFile()` is never called afterwards (`WPAudioWriter.cpp:137-141`,
  `close()` returns at `:240-242`): a leak and a silent failure.
- **T19 (medium-low): parallel readers for a long compressed file's peak
  build**; then `TIOIdleTask::weight()` goes. Design note:
  `peak-records.md` section 7 (Remon, 2026-10-31), after the release.
- **T20 (done 2026-10-28: merged `9ab838d7`; built otherwise than planned, see Built): `TChannelBuffer` operations auto-vectorised, hand SIMD
  retired.** Measure first. Three variants on Linux: the portable loops,
  the SSE assembly, and AVX2 through `target_clones("avx2", "default")` on
  the portable loops (Remon + review, 2026-10-28). Then decide which stay.
  - The test needs its own reference: a plain scalar loop in the test,
    never cloned. With `target_clones` on `portable_*`, "the portable
    version" is the AVX2 clone on an AVX2 machine, and the test would
    compare AVX2 with AVX2.
  - FMA: with an "avx2,fma" target, or contraction allowed (GCC's default
    in GNU mode is `-ffp-contract=fast`), `dst += src * gain` skips a
    rounding and differs from SSE in the last bit. Compare with a
    tolerance (the test has 1e-6 for gain and mix, peaks exact), or
    `-ffp-contract=off` for that file.
  - `target_clones` needs ifunc: Linux with glibc only. macOS has
    Accelerate (and Apple Silicon is ARM); Windows would need a separate
    TU with `/arch:AVX2` and a choice through `TCpuFeatures` (CPUID leaf 7
    plus the OS check with `xgetbv`): only if the Linux measurement shows
    it is worth it.
  - **Built** (measured first: `doc/design/t20-buffer-operations-measurement.md`):
    the SSE assembly is gone (`sse_functions*.S`, `WANT_SSE`,
    `SSE_OPTIMIZATIONS`). No `target_clones`: the same loops
    (`TBufferOperationsLoops.h`) in their own translation unit with
    `-mavx2` (x86-64 with GCC or Clang, MinGW too; not macOS, which keeps
    Accelerate), chosen at run time through `TCpuFeatures::avx2` (CPUID
    leaf 7, XGETBV); the compiler's target decides whether the file is
    built. The peak in the GCC/Clang vector extension (no compiler
    vectorises a maximum in one chain), `maxps` on x86, 4 lanes portable
    and 8 in the AVX2 file; the mixes with `__restrict` (GCC's -O2, as
    Debian builds, refuses the overlap check otherwise; safe since T64).
    No -O3 needed. The test compares every implementation the machine
    runs with its own scalar loops, exact on x86 (no FMA there).
    Freewheel throughput of Minidisk opname: master 814×, portable 902×,
    AVX2 1189×. 32-bit x86 dropped with it (`456432df`).
- **T21 (low, Ben): Traverso's AAC writer** drops the first 2048 frames and
  writes raw ADTS into `.m4a`.
- **T22 (low): the export dialog's MP3 and Ogg options** do nothing (fixed
  VBR quality); map them to libsndfile or drop them.
- **T23 (low): the waveform library's error style** (`TPeakFile::Status` +
  `QString*` + sticky `error()`, `TPeakStream.h:55-70`) follows T9, so one
  style remains.
- **T24 (low): an MP3 whose last frame is cut** is now an error at its tail,
  and every read there costs a seek with preframes (the tag says more than
  the scan found, the read fails where the audio ends).
- **T25 (low): FAAD's seek pre-roll assumes AAC-LC**: HE-AAC may decode
  wrongly after a seek; needs a test file, or document the limit.
- **T26 (low): `sf_strerror(nullptr)`** reads libsndfile's global last-open
  error (`SFAudioReader.cpp:46`): with readers opened at the same time on
  worker threads the message can belong to another file. Verify, with T9.
- **T27 (low): `TAudioReader::output_frame_count()`** at another rate can be
  one frame high for file rates that do not divide the universal rate (37.8,
  50 kHz): the length is rounded up by less than one unit. With the timebase
  task.
- **T28 (very low): recovering a damaged file** (what to do when the decoder
  keeps returning errors).

### Not ranked yet (Remon places them)

From the audiofileio API review (`doc/design/audiofileio-api-review.md`,
2026-10-27):
- **T44 (done 2026-10-28: merged `660ab5ea`, failures as KNOWN entries T46–T48) A round-trip test of every writer and reader** (high; Remon,
  2026-10-27): every writer × data format × 1, 2, 6 channels through
  `TAudioWriter` and back through `TAudioReader`; float exact, integer
  within the dither, lossy by level, placement and channel order. W0 of the
  API work order, built first so it checks W1–W4.
- **T29 (done 2026-10-28: W0–W4 reviewed by Remon and Fable, merged `660ab5ea`) The API review's work order** (no rank: approved by Remon once Fable
  approves the document, built after midnight): W1 buffers (the scratch
  into the library, `TAudioBuffer` → `TChannelBuffer`, `TFileIOBuffer` →
  `TAudioBuffer`, the threading sentence), W2 the writer stops after a
  failed write, W3 the buffers' hygiene and small header cleanups, W4 the
  resampler choice as one typed value in `support`. I2 joined T15.

From the Fable review of the `audiofileio-errors` branch (2026-10-27, existing
code):
- **T30 A recording on a full disk** keeps calling `rb_file_write()` per slot
  after a write error (`TBufferedAudioStreamWriter.cpp:158-162`): one log
  line per slot, and the stream does not stop (with T10).
- **T31 `FaacAudioWriter::write()`** returns 0 after a failure mid-loop although
  earlier chunks were written (`:218-246`).
- **T32 `WPAudioWriter::write_block()`**: a short `fwrite` without `ferror`
  leaves a stale `errno` in the detail (`:145-149`).

- **T33 Discuss first:** `TAudioReader`'s resume after `read_file_frames()` (KISS:
  keep or plain reposition) and `set_output_rate_and_resampler()` repositioning
  with an index at the old rate.

Other open tasks:
- **T34 Freewheel as its own task**: export and the bounce track run on it;
  bounded waits; close while freewheeling hangs (reader waits up to 1 s per
  stream per cycle, then a modal dialog in `TProject::disconnect_from_audio_device()`).
  With it (Remon, 2026-10-28, a mixdown in freewheel on PipeWire): the
  watchdog reports "the PipeWire audio thread stopped processing" and
  switches to the Dummy driver (`TAudioDevice.cpp:1016`); freewheel on
  PipeWire does not work (not yet analysed).
- **T35 Split `TSheet::transport_control()`** into an audio-thread callback and a
  GUI function (removes 3 of the 7 Clang `-Wfunction-effects` reports).
  With it (TAudioDevice review, 2026-10-28): `transport_start/stop/locate`
  call the client's real-time callback on the GUI thread and share
  `m_transportControl` with the JACK slave path on the audio thread.
- **T36 Clip fader offset in the start cycle**: the gain curve is shifted by
  `offset` frames in a clip's first cycle (FIXME in `TAudioClip.cpp`).
- **T37 Readahead and sample-exact clip edges** (reads more frames than processed
  at a clip edge; stream windows that do not hold the clip's window).
- **T38 Recording data safety**: header length 0 until close; risk analysis and
  ROI first (`recording-data-safety.md`).
- **T39 Zoom state into a controller** (branch 3); **PlayHeadMove as preview →
  commit**; **feedback for targeted items**; **waveform presentation logic to
  the model** (with the Qt Quick canvas); **waveform display modes**.
- **T40 Internal routing and routing via JACK/PipeWire** (incl. JACK transport
  slave); **PipeWire as a graph client**; **JACK revision** (port lists as
  `TRtList`).
- **T41 Export through the bounce track** (own branch).
- **T42 Project file converter**: upgrade when the version is bumped; removes the
  default fades of older versions.
- **T43 Gain ramp**: click-free mute, solo and fader (ramp state per transport).
- **T45 Encode settings come with sane defaults** (proposed low; Remon
  2026-10-28): whoever makes a `TAudioEncodeSettings` is forced to start from
  sane defaults, never undefined; what the user then chooses is the user's.
  Today it nearly works that way: `TExportSpecification` is the only maker in
  the app and its constructor sets soxr at its default quality; the export
  dialog takes the configured backend (`Conversion/Resampler`), but a
  recording keeps the constructor's soxr whatever the config says. Choosing
  the backend in the GUI is planned (settings page).
- **T46 FLAC 8 bit reports 2^32 - 1 frames** (proposed medium): a FLAC file
  written by `TAudioWriter` at 8 bit reads back with a frame count of
  4294967295 (writer or reader, libsndfile; 16 and 24 bit are right). Found
  by the round-trip test (T44); this made the first run take 13 GB
  (2026-10-28). KNOWN entry in `test_audio_round_trip.cpp`.
- **T47 AIFF 8 bit** (proposed medium-low): samples come back off by up to
  1.0 (a sign or offset error) and mono has one frame more (the pad byte of
  an odd-sized chunk?). Round-trip test, KNOWN entry.
- **T48 AAC channels** (proposed medium-low, with T21): mono written by FAAC
  reads back as 2 channels with FAAD (its upmix: FFmpeg reads 1 channel); with 2
  and 6 channels the levels, the placement and the channel order are wrong.
  Round-trip test, KNOWN entries.
  - **Found 2026-11-01**: `FaacAudioWriter` never looks at `dataFormat`. It tells
    the encoder 16 bit and reads the buffer as `int16_t`, while `TAudioWriter`
    packs it in the format asked for, so 8, 24, 32 bit and float encode the wrong
    bytes. The export dialog forces 16 bit for M4A, so only the API reaches this.
    The writer must take 16 bit only, or convert.
  - **With FFmpeg's reader** the mono round trip goes on to levels and placement:
    9 lines fail, 16 bit only on placement, lag -2048, which is T21.
  - **To check**: whether these two causes explain all of the 2 and 6 channel
    entries. Mono's KNOWN entry goes when FAAD does.
- **T49 What the writer refuses** (proposed low, with T22): libsndfile
  refuses WAV and W64 at 8 bit (they take unsigned 8 bit: map PCM_S8 to
  PCM_U8?), FLAC float and 32 bit (not in FLAC: should the dialog offer
  them?), and every Ogg Vorbis and MP3 combination through `TAudioWriter`
  (the sub-format mapping). The round-trip test lists the refusals.
- **T50 `TAudioFileMerger` with an empty source** (proposed low): the first
  pass of its do-while runs with 0 frames and indexes an empty channel list
  (`TAudioFileMerger.cpp:108-116`; before W3 it failed one line later, at
  `TQueueBufferSlot(0, …)`). Guard the merge on a zero-length source.
  (Fable's review of the API branch, 2026-10-28.)
- **T52 (done 2026-10-28: merged `28216d8e`; names: `TBufferOperations`, `TFaderLaw`, `TGain`, `TCpuFeatures`) The mix functions work without the application** (proposed
  medium; before T51; Remon 2026-10-28). `Mixer::compute_peak`,
  `apply_gain_to_buffer`, `mix_buffers_with_gain`, `mix_buffers_no_gain`
  are nullptr until `Traverso::init_sse()`: a headless dsp or core test
  crashes at the first mix or peak. Change: the pointers `constinit` to the
  `default_*` functions; one dynamic static initializer in `Mixer.cpp`
  picks SSE or Accelerate before `main()` (no threads yet, no race; not on
  first use, which can be the audio thread); no log there (TLog may not
  exist yet): the choice is logged later through a name getter.
  `Traverso::init_sse()` goes. Test in `tests/dsp`: mix, gain and peak
  without the application, and the chosen functions against the defaults
  on random buffers of several lengths and unaligned pointers (the SSE
  assembly under test at last; the base for T20).
  Names, agreed 2026-10-28, as the FIRST commit of T52 (names only, all
  green; then the behaviour change as a small diff of its own), with the
  rows in `doc/design/renames.md` in that commit:
  - `Mixer` splits: the operations on a buffer's samples →
    `TBufferOperations` (not "kernels": a filter kernel in DSP, and not
    all four mix; not "sample": a sound snippet in a DAW; Remon
    2026-10-28); the fader position mapping and its limits → `TFaderLaw`.
  - `compute_peak(buf, nsamples, current)` → `update_peak(buf, nframes,
    current)`; `apply_gain_to_buffer` → `apply_gain`;
    `mix_buffers_with_gain` / `mix_buffers_no_gain` → `mix_with_gain` /
    `mix`; `default_*` → `portable_*`; `x86_sse_*` and `accel_*` stay
    (assembly symbols, T20). `typedef ..._t` → `using`.
  - dB ↔ gain is audio domain, not fader: free functions in dsp,
    `dB_to_gain()` (was `dB_to_scale_factor()`) and `gain_to_dB()` (was
    `Mixer::coefficient_to_dB()`), used by curves and automation too.
  - `TFaderLaw`: `dB_to_position()`, `position_to_gain()`, `clamp_dB()`,
    `clamp_gain()`; the limits `constexpr`: `MinDB`, `MaxDB`, `MinGain`,
    `MaxGain`. One spelling: the B of dB is always capital (`dB_` in
    snake_case, `DB` in CamelCase), so `ZeroDbPosition` → `ZeroDBPosition`.
  - `FPU`, `fpu.h`/`fpu.cc` → `TCpuFeatures`, `.h`/`.cpp`; with T51
    `TDenormals::prepare()` (off the audio thread) and `enforce()` (every
    cycle).
  - `f_max` stays: `std::max` ignores a NaN where the trick passes it on
    (a meter would hide T8's NaN), and whether it vectorises better is
    T20's measurement, not a rename.
  - **Built** (where it differs from the names above): dB ↔ gain is
    `TGain` (`from_dB()`, `to_dB()`, `clamp()`, `clamp_dB()`), not free
    functions, so `clamp_dB()` is in `TGain` and `TFaderLaw` has
    `dB_to_position()` and `position_to_gain()`; `TDenormals::decide()`
    runs before `main()` instead of a `prepare()`. `f_max` went with T20
    after all (Remon, 2026-10-28): the SSE assembly in use on x86 already
    hid a NaN (measured: except at the last frame), so the reason above
    did not hold; a NaN is kept out of the buffers by T8, and the meter
    does not show one.
- **T51 (done 2026-10-28: merged `28216d8e`; `TDenormals`, DAZ from the FXSAVE mask, FTZ also on the main thread; tested on PipeWire and ALSA, JACK not) Denormal protection on every audio thread** (proposed medium-high;
  after T52; Remon 2026-10-28). `Traverso::setup_fpu()` clears FTZ and then
  sets 0x8000, which is FTZ again (DAZ is 0x0040): net FTZ on a CPU that
  reports DAZ, DAZ never. It runs once on the GUI thread; threads made by
  CoreAudio (possibly PipeWire, JACK) do not inherit it. And it hangs on
  `SSE_OPTIMIZATIONS` (`UNIX AND WANT_SSE` and a known architecture):
  Windows, `WANT_SSE=OFF` and probably macOS have no protection at all.
  Change: one function in dsp (with `fpu.h`), on `__x86_64__`/`_M_X64`
  (and `__SSE__` for 32 bit), independent of `SSE_OPTIMIZATIONS`. The mask
  (FTZ, plus DAZ where the CPU has it, 0 under
  `TRAVERSO_RUNNING_UNDER_VALGRIND`) is decided once off the audio thread
  (`cpuid`, `getenv`); every cycle `TAudioDevice::run_cycle()` (all drivers
  go through it) reads MXCSR and writes it only when bits are missing
  (`T_REALTIME_SAFE`; catches new driver threads and plugins that change
  MXCSR). `Traverso::setup_fpu()` goes. Test: a Dummy-driver cycle in a
  process without `Traverso` reads FTZ and DAZ on the audio thread; clear
  them on the audio thread, the next cycle sets them again. Runtime check
  on ALSA and JACK (CoreAudio when someone can). Not in scope: the IO and
  peak threads (denormals cost throughput there, no glitches); ARM:
  FPCR.FZ, later (Debian ARM builds, Apple Silicon).
- Post-merge leftovers of the peak work: remove the recording post-processing
  glue, check mapping a growing file on Windows and macOS, fix the sentence in
  `diskio-rounds.md` §5 **(check whether done)**.
- Small known bugs: `TViewPort::leaveEvent` FIXME (crash on Enter after a
  context menu); `TConfig::load_configuration` runs twice on first start;
  `TAudioDriver.cpp:64` shadows `sleepTime` (the subtraction on `:68` does
  nothing): fix with the test audio driver (work order step 8).

TAudioDevice review (Remon, 2026-10-28; proposed, Remon ranks):
- **T53 (done 2026-10-28: merged `73dad650`) Races in TAudioDevice** (proposed medium-high):
  `m_processCallBackWaitTime` is added to on the audio thread
  (`run_one_cycle()`) and read and zeroed by the GUI (`get_cpu_time()`),
  not atomic. `m_isRealTime` and `m_processCallBackData.set_real_time()`
  are written by the GUI (`set_free_wheeling()`,
  `driver_changed_free_wheel_mode()`) while the audio thread reads them.
  `m_xrunCount` is written by the audio thread (`xrun()`), reset by a GUI
  timer and read in `audiothread_finished()`. Fix: atomics, or through the
  message queue. No structural change.
  How (Remon, 2026-10-28): the real-time flag is a `std::atomic<bool>` the
  GUI writes; `run_cycle()` copies it into `TProcessContext` at the start
  of every cycle (as `TDenormals::enforce()`), so the context stays a plain
  struct only the audio thread writes, and the cycle uses the copy (also
  for `m_isRealTime`'s own reads, `TAudioDevice.cpp:323,336`). The wait
  time like the CPU time: an atomic, `fetch_add` on the audio thread,
  `exchange(0)` on the GUI; the CPU time's `load()` + `store(0)` gets
  `exchange(0)` too (a `fetch_add` between them is lost now). TSan in its
  own build next to the RTSan one; CMake refuses both at once. Branch
  `audiodevice-races` with T54 and the safety net of T58 step 1.
- **T54 (done 2026-10-28: merged `73dad650`; a flag the GUI watchdog timer takes, untested until the test audio driver) `xrun()` stops the driver from the audio thread** (proposed
  medium): `m_driver->stop()` on a storm runs on the thread the driver
  drives. Stop it from the GUI thread (the storm signal already goes
  there).
  Right after T53 on the same branch (same `m_xrunCount`, same storm
  path).
- **T55 `TAudioDriver::attach()` gives playback channels the capture
  latency** (proposed medium; `TAudioDriver.cpp:114`, copy-paste).
- **T56 Drivers use the singleton while they hold the device** (proposed
  low; `TAudioDriver.cpp:123,131`: `audiodevice().create_channel()` where
  `m_device` is there). Part of T58.
- **T57 Channel ownership in three places** (proposed low): the device
  creates, the driver lists, the driver's destructor deletes through the
  device, `shutdown()` removes them first, the device's destructor deletes
  the rest. One owner: the driver.
- **T62 A stale `QThread::finished` gives a second fallback** (proposed
  low; Fable's review of `audiodevice-races`, 2026-10-28; on master too,
  to verify with a log): after a storm a blocking driver's thread ends on
  its own, so `finished` is queued before `shutdown()` disconnects it; it
  arrives after `switch_to_null_driver()` started the Dummy driver, passes
  `audiothread_finished()`'s guard and restarts the Dummy driver once
  more. Fix: a generation in the connect lambda, not `sender() ==
  m_audioThread` (the freed address may be reused).
- **T63 Off the audio thread into the audio thread's queue** (proposed
  low; same review): JACK calls `xrun()` from its notification thread,
  which posts `bufferUnderRun` into `m_postedFromRTThreadQueue`, a
  single-producer ring, next to the audio thread's own posts (the storm
  no longer posts since `audiodevice-races`). And `set_free_wheeling()`
  stops and starts a driver without native freewheel from the GUI while
  the audio thread runs it (benign on the Dummy driver).
- **T65 The device's rate before the driver granted it** (proposed low;
  Remon 2026-10-28): `set_parameters()` sets `m_rate` to the requested
  rate (`TAudioDevice.cpp:371`) before the driver reports the rate it got
  (`set_sample_rate()`); in between the requested rate is "the rate", also
  when the hardware does not give it. `m_rate` only from the driver. Rate
  model: project (the user's wish), runtime requested (`m_setup`), runtime
  granted (`m_rate`, `TProcessContext`), source (the file's rate); outside
  audioruntime only the granted rate is read, the requested one only by
  the settings dialog and the project save. Also: `set_parameters()`
  writes `m_rate` directly while the cycle's context gets the rate only
  through `set_sample_rate()`; one path, so the two cannot drift.
- **T66 A fallback overwrites the project's driver wish** (proposed
  medium; Remon 2026-10-28): after a fallback `m_setup` is the Dummy setup
  with the granted rate (`fallback_setup()`), and `TProject` saves
  `get_device_setup()` (`TProject.cpp:1445`): saving after a fallback
  writes the Dummy driver into the project, the wish is gone. Against
  "an existing project's rate never changes silently" (decisions
  2026-10-28).

- **T64 (done 2026-10-28: merged `094f3e1b`) Feedback loops in the routing: a bus track can send to itself**
  (proposed medium-high; found 2026-10-28 for T20's `__restrict`; Remon:
  every feedback loop must be excluded): a send to a bus track mixes into that
  track's process bus (`TProject::get_audio_bus()`), and a track's send
  mixes from its own process bus (`TTrack::process_send()`), so a bus
  track with a send to itself mixes a buffer into itself. The send menu
  leaves the track itself out for the masters but not for the project's
  bus tracks (`TTrackManagerDialog.cpp:258`); `TTrack::add_post_send()`
  and `add_pre_send()` only refuse a duplicate, and a project file is
  loaded as it is. Loops through more bus tracks (A to B to A) are not
  refused either: no overlap, but feedback. Fix: `TTrack` refuses a send
  that closes a loop (the target, or a track it sends to, directly or
  further on, is this track; one place for menu, dialog and load), the
  menu leaves those targets out, and `Q_ASSERT(&dest != &src)` in
  `TChannelBuffer::mix_buffers_*()`. Before T20's `__restrict` (with it
  the self-mix is undefined behaviour).
- **T67 Sends only between registered tracks: load and remove/undo**
  (proposed medium; found 2026-10-28 during T64 and in its review; merged
  into one task by Remon). Both halves are sends that exist while their
  track is not registered in the session:
  - **Load** (read in the code, not reproduced): a track's sends load in
    its `set_state()`, and `TSend::set_state()` finds the target through
    `TProject::get_audio_bus()`, which only knows the bus tracks registered
    so far (a track registers after its `set_state()`: `TProject.cpp:409`,
    `TSheet.cpp:236`). A send between two project bus tracks, which the
    send menu allows, survives a reload only when the target comes first
    in the file.
  - **Remove and undo** (Fable's T64 review, traced in the code):
    removing a track only takes it out of the lists
    (`TSession::private_remove_track()`), its sends and the sends to it
    stay. T64's loop check does not see a removed track, so with PB1 to
    PB2 to PB3, removing PB2, adding PB3 to PB1 and undoing the removal
    gives the loop PB1 to PB2 to PB3 to PB1. Already before T64: while PB2
    is removed, PB1 still mixes into PB2's bus, which nothing processes or
    silences.
  - Fix: load creates and registers all tracks first, then restores the
    sends; removing a track also removes its sends and the sends to it,
    and the same command restores them on undo (a removed track has no
    routing). T64's check then sees every send that can come back.
- **T68 (done 2026-10-29: `read()` touches `m_partPath` only once there are blocks) `TPeakStream::read()` reads `m_partPath` while `open()` may write
  it** (proposed medium-high; from the review of the grid-walk work order,
  traced in the code 2026-10-29). `TPeakBuildService::build()` hands the
  stream out at once, `TWaveform::start_build()` keeps it as `m_building`
  (`TWaveform.cpp:160`) and paints from it on every repaint
  (`TWaveform.cpp:211-213`); the build job calls `open()` later on the
  DiskIO thread (`TPeakBuildTask.cpp:117`), which assigns `m_partPath` up to
  a hundred times (`TPeakStream.cpp:80-90`). `read()` constructs
  `QFile file(m_partPath)` before it checks `blocks > 0`
  (`TPeakStream.cpp:334`): a `QString` copied while another thread assigns
  it. Once `blocks > 0` the release store of `written_frames()` orders it.
  Fix: touch `m_partPath` only after `blocks > 0`.
- **T69 Stale comments: "from one frame per pixel on"** (proposed low; same
  review): `TWaveform.h:76` and `TAudioClipView.cpp:815`; the grid starts
  above 32 frames per pixel (`TWaveformColumns.h:30`, `MinShift`).

From the GPU waveform test program, clip edges in variant A (branch
`gpu-waveform-poc`, report `~/Development/Traverso/git/gpu-waveform-poc-results/rapport-clipranden.md`,
2026-10-29/30):
- **T70 The part-blocks at clip edges are clip data** (low; Remon,
  2026-10-30). A clip edge that is not on a 64-frame block boundary lies
  inside a level-0 block. That block's record also holds up to 63 frames
  from outside the clip, so the pixel with the edge must come from the
  samples of the part inside the clip: otherwise it shows a peak up to 63
  frames (1.3 ms) outside the clip (a cut just before a transient shows the
  next hit in the clip), or it loses a peak in the first 63 frames inside.
  Only the first and last pixel of a clip depend on it, but that is the
  pixel an editor looks at when cutting. The part-block depends only on the
  edge, not on the zoom or the scroll position.
  - **Why a task**: the test program reads the part-blocks from the samples
    at every gather (`TAudioReader::read_file_frames()`), and for compressed
    sources that is a seek plus decode: measured per edge WAV 2–9 µs, FLAC
    ~0.3 ms (max 1.8 ms), MP3 ~1.4 ms (max 23 ms). It is a problem of the
    audio clip domain: every view with exact edges needs it, and moving an
    edge changes it at every step.
  - **When the user sees a hitch** (a frame of 30–110 ms instead of 8 ms;
    measured with 26 lanes, 78 clips of WAV/FLAC/MP3, vsync 120 Hz):
    (1) zooming, at every change of pyramid level (every doubling of the
    zoom): 36 ms on average, up to 108 ms a level change; (2) scrolling
    fast or a jump in the timeline, each time the view leaves the gathered
    records (19 times in 8 s at one screen a second, up to 72 ms); (3)
    dragging a clip edge, at every step, in an MP3 up to ~23 ms a step;
    (4) opening a project, once for all visible clips. Playback with the
    view following does not hit it (the records reach ~24 s ahead), nor a
    view that stands still.
  - **Fix** (not built): keep the min/max/RMS of each edge's part-block
    with the clip, computed on load, trim or a move in the source, off the
    GUI thread (the IO thread that serves the peak files); while an edge is
    dragged, keep a decoded window around it so each step reads memory. Two
    records per clip.
  - **Check first**: whether the sheetcanvas has the artifact today.
    `device_columns()` falls back to `query()` at clip edges, and `query()`
    gives a block to the column that holds its first frame, without the
    samples of part-blocks: the last pixel may show up to 63 frames after
    the clip, the first may lose up to 63 frames inside it. Not tested; the
    clip edge test of the test program (`clipedges/`) can run through
    `device_columns()`.
- **T71 Reading and decoding apart: `TAudioByteSource` (now `TByteSource`)** (Remon, 2026-10-30;
  branch `audio-byte-source`). Rule: no disk access the scheduler does not
  know about.
  - **Open questions for step 2c:** `doc/design/byteio-open-questions.md`
    (mlock, readahead and buffer management, Qt in byteio).
  - **Step 1, built:** every decoder reaches its file through one byte
    source; open, seek and stream counted apart, page cache apart from the
    medium, decode time = call time − read time. No scheduler change.
  - **Step 2a, built:** file offsets of 64 bits, also on 32-bit ARM
    (`_FILE_OFFSET_BITS=64` for every library in `CMakeLists.txt`, a
    `static_assert` on `off_t` in `TByteSource.cpp`).
  - **Step 2b, built:** a seek of a stream is a full reset with a
    generation (`TIOStream::next_generation()`, `stale()`,
    `forget_position()`, the scheduler's bump in `TIOScheduler.cpp`): a
    refill in flight across a seek stops at its next `stale()`.
  - **The slot size (T58):** `device-ownership` was merged first
    (2026-10-31); this branch was rebased onto it and keeps its
    `m_bufferSlotFrames` in the seek and in the refill (one source).
  - **Step 2:** `prefetch()` into a window in own memory, with a floor of
    16 KB and a margin that grows after a miss; exact byte ranges for PCM,
    libmpg123 and FAAD; the counter "unplanned reads", target zero.
    The kernel's readahead hides the medium in step 1 (a slow decoder finds
    its pages read already): the prefetch must be the only reader, for
    instance `posix_fadvise(POSIX_FADV_RANDOM)` on the stream's file, only
    with the prefetch in place (without it the library's small reads lose
    their readahead). Measure both.
  - **Step 2c (read pool per medium, decode pool):** ioscheduler becomes free
    of Qt, as part of the redesign of the pools.
    - Look again at who owns the read counters: now the decoder, with a bare
      pointer in `TByteSource`; with a read pool the source could hold them and
      the decoder read them out (Remon, 2026-11-01).
    - When the own pools are in: remove the two hand-over lines
      (`race_top:TIOScheduler::start_stream_step`, `start_task_step`) and
      the `QThreadPool` lines from `tests/tsan.supp`, and run TSan again.
    - `QThread`, `QThreadPool` and `QRunnable` become `std::thread` with
      pools of our own. No new pools on top of Qt.
    - `QObject` and the signal `seekFinished` become a plain callback. Core
      itself sees to it that the message arrives on the GUI thread (now
      through `Qt::QueuedConnection`, `TSheet.cpp:145`).
    - `s_handOverFence` (`TIOScheduler.h:274-278`) exists only because TSan
      does not see `QThreadPool`'s lock. With pools of our own it can go;
      then verify the hand-over contract (`TIOScheduler.h:78-80`) with TSan
      again.
    - `idle_limit_for()` in `TIOScheduler.cpp`, with its `QString` and
      `QFile`, may stay in this step: it belongs to the detection of the
      medium.
    - Platform details go into one small helper in support, next to
      `TPlatform.h`; the pools themselves stay pure standard C++. The helper
      holds only:
      - The thread's name at OS level. Mind the differences:
        `pthread_setname_np` has another call on Linux than on macOS, Windows
        uses `SetThreadDescription`.
      - The thread's priority.
      - The IO priority: `ioprio_set` on Linux; on macOS and Windows the
        equivalent, or a documented no-op.
      - The number of cores: `std::thread::hardware_concurrency`, with a
        fallback when it gives 0.

      Plain functions, no class hierarchy or interface per platform. Each
      function says which platforms it was tested on; macOS and Windows
      cannot be built here, so those branches are marked untested. No other
      functions than these four without consulting Remon.
    - Touches waveform: `TPeakCache.cpp:247` uses
      `QThreadPool::globalInstance()` once. Noted, not changed in this step.
  - **Step 3:** two phases in `fill_realtime_buffers()` and the peak build
    (meets T4 and T6).
  - **Step 4 (after T58, T60):** disk slots and a decode pool (T61); a miss
    asks the scheduler; opening and scanning as an idle task; the MP3 seek
    index kept next to the peak file (own task).
  - Scenario files are still read through `QFile`: a small text file, no
    audio.
  - From Remon's review (2026-10-30), not urgent:
    - Opening reads whole files: `mpg123_scan()`, and FAAD scans an
      ADTS file through, byte by byte at a mismatch. The 16 KB block keeps
      it bearable; the real answer is the seek index next to the peak file
      (step 4).
    - The `(off_t)` cast at `SFAudioReader.cpp:141` (`sf_seek` takes
      `sf_count_t`).
    - FAAD makes a new `QByteArray` per packet: allocations per decode.
    - The MAD points fell away with T73 (2026-10-31): `size()` asked at
      every buffer fill, `K3bMad::decodeNextFrame()`, the pre-roll by
      bitrate (now `MPG123_PREFRAMES`). The whole read at the open stays,
      through `mpg123_scan()`, above.

- **T73 (done 2026-11-01: merged after three Fable reviews)
  `MpgAudioReader`: MP3 on libmpg123, MAD removed** (Remon, 2026-10-31).
  Built as `doc/design/mp3-decoder.md` says, with the new tests listed there;
  the decisions are in that document and in `doc/decisions.md`.
  - Two reports for mpg123 exist as drafts; Remon sends them himself.
- **T75 `interleaved_scratch()` in three files** (low; Remon, 2026-10-31):
  `SFAudioReader.cpp`, `MpgAudioReader.cpp`, `TAudioWriter.cpp`. One place, in
  the clean-up after the release.
- **T77 Reading around damage in an MP3** (parked; Remon, 2026-10-31). Only when a
  real file asks for it: read a damaged file and skip just the broken parts, where
  now the read stops at the damage. Accepted for now: a leaner and more robust
  API and library, at the price of fewer files read than MAD did.
- **T78 The reason of a failed open does not reach the user** (Remon, 2026-11-01).
  A drag onto the canvas is refused without a word (`TClipsViewPort.cpp:110` asks
  `can_read()`, a yes or no). In a project or an import the user sees "Could not
  open file": `TBufferedAudioStreamReader` keeps an error code, the reason goes to
  the log only. Keep the `TAudioFileError` in the stream and show its detail; let
  a refused drag say why.
- **T79 Frame positions past 32 bit** (research; Remon, 2026-11-01). `nframes_t` is
  `uint32_t`: 27 hours at 44.1 kHz, 6.2 at 192 kHz. Above it `MpgAudioReader`
  refuses, `SFAudioReader` and `WPAudioReader` cut the length silently. To find
  out: all of `nframes_t` to 64 bit in one go, signed (`int64_t`) or unsigned;
  WavPack to its `64` calls (also T17); armhf and the three `atomic<nframes_t>`;
  the borders with JACK (32 bit) and PipeWire (`uint64_t`). What can be negative
  keeps a signed type. `-Wsign-conversion` on the targets it touches. A step in
  between: SF and WavPack refuse above the limit.
- **T80 A sample library as a test** (Remon, 2026-11-01). Real and damaged files in
  `~/Music/TraversoSamples/` (local, never in the repository; `manifest.tsv` has
  source, license and result per file). A test target `audio_corpus` that runs only
  when the folder is there (path through a variable): read, seek, build peaks,
  resample per file, also under TSan. Then a project `TraversoSamples` for the
  application: import, waveforms, playback. Not done yet: so far every file only
  went through `TAudioReader` one by one.
- **T81 Judge the files of the sample library** (Remon, 2026-11-01). A file refused
  wrongly is a bug and gets fixed. A damaged file that may be usable is judged on
  its use in Traverso. A file really broken, or without audio, is refused. The
  message to the user is clear and says what is wrong (with T78). The judged
  outcome per file becomes the expectation of `audio_corpus`. First with `ffprobe`
  per file. Candidates: `nero-chapters.m4b`, `bear-mpeg2-aac-only_frag.mp4`,
  `apev2-lyricsv2.mp3`, `synth-bad.mp3`; the vague "FAAD cannot read it".
  Work list from all samples against ffmpeg (2026-11-01; a rough comparison, to
  check by hand):
  - possibly refused wrongly: `apev2-lyricsv2.mp3` (ffmpeg reads 85295 frames),
    `lame397v9short.mp3` (with the reason "-300 frames of audio");
  - shorter than ffmpeg: `double-sfx.ogg` (15435 against 30742, chained Ogg),
    `populated-zero-value-padding.ogg` (48000 against 96000, ffmpeg reports CRC
    errors), `icy_sfx.mp3` (11025 against 14976);
  - opens but reads nothing, better refused: `52-overwritten-metadata.flac`,
    `flac_application.flac`, `ooming-header.flac`;
  - place: `vbri.mp3` (1152 frames), `Example.mp3` (529, an iTunes tag with
    delay 0), `bear-opus.ogg` (312, pre-skip);
  - `bear-audio-10s-CBR-has-TOC.mp3`: the same length, 33 dB apart.
- **T90 Video** (parked; Remon, 2026-11-01). A PoC closed, branch `video-poc` stays as a
  reference, not merged. byteio and the I/O scheduler carry video: warm no missed
  refill next to 32 tracks, 4K on one thread, one line in core. If it goes on: video as
  idle work or at least below audio; as idle tasks a seek of the audio takes 3 ms
  against up to 2 s, but idle work stops while a song records. Open: video while
  recording, peak builds in the same queue, a TVideoTrack apart from `TTrack`, the
  link between a video clip and its audio clip, the sync (never judged). Missing in
  the scheduler: a stream of 0 channels is left out without a word, `TIOStream` is an
  audio interface, a group's seek waits for its slowest stream, a step has no bound.
- **T83 A rare race in `TIOScheduler`** (Remon, 2026-11-01). `io_scheduler` failed
  once under TSan on 2026-11-01 with 22 reports (`TIOScheduler.cpp:44`, `:71`,
  `TIOScheduler.h:289`, `:296`), then passed 6 of 6. Not from FFmpeg's reader.
  Find out what it was.
- **T82 FFmpeg as the required decoder instead of FAAD** (Remon, 2026-11-01). An
  `FFmpegAudioReader` on libavformat and libavcodec, a required dependency, also
  when a distribution's build has fewer codecs. FAAD and the MP4 code of our own
  go; no second AAC path, not per file and not per system. Merged 2026-11-02.
  `FaadAudioReader` stays in the tree and is built, but nothing reaches it, until
  Remon has spoken to Ben, whose code it is.
  - **To show**: exact seeks and length by the contract (`test_audio_decoders`,
    `audio_corpus`), access through `TByteSource` (`AVIOContext`), one lower bound
    without `#if`.
  - **A build without the decoder**: refuse with a reason that names the format as
    the file gives it, and that this FFmpeg has no decoder for it. No tested
    distribution needs this today. Implicit HE-AAC shows as LC in its container.
  - **Measured 2026-11-01**: Fedora 44's `ffmpeg-free` 8.1.3 and Tumbleweed's
    FFmpeg 9.0.2 use the native `aac` decoder. On Fedora all 26 AAC samples came out
    byte for byte as FFmpeg 8.0.1, also HE-AAC v1, v2 and Main. Fedora's other
    decoder, `libfdk_aac` of fdk-aac-free, gives half the samples on implicit HE-AAC
    without an error and refuses Main, so the reader asks for `aac` by name, not for
    what the codec id finds first.
  - **Versions**: FFmpeg 8.0 and 9.0 are tested, 8.0 is the lower bound for now
    (Remon, 2026-11-01): the releases Traverso first lands in have 9.0, Ubuntu 26.04
    LTS and this machine 8.0.1, and stable releases with 7.1 or older do not get it.
    The requirement can change with the outcome of those tests. Measured with the
    PoC built against both: 9.0 adds an end skip to the last packet, 8.0 lacks it, so
    the length comes from the rule below and the reader reads the same on both.
  - **Length of an MP4** (Remon, 2026-11-01): compare the header's length with the
    tables' end, both after the priming; one packet is the index's own packet length.
    - apart by at most one packet: the smallest of the two, healthy;
    - tables more than one packet below the header: open with the header's length,
      damage, the read fails at the end; FFmpeg shortens the header to the tables
      when it reads them (`mov.c`), so a table cut short reads as a shorter file:
      what the tables hold is the file. Audio cut from the file keeps its length
      and fails at the end;
    - tables more than one packet above the header: refuse with a reason;
    - header missing, zero, negative or above `nframes_t`: refuse with a reason;
    - fragmented MP4 under the same rule, but FFmpeg walks every fragment at the
      open, so its header says what the walk found and the open reads the whole
      file (Remon, 2026-11-01: accepted); ADTS has no length and keeps counting.
    - the decoder leaves out what the file asks to skip and moves the stamps with it;
      frame 0 is where the first packet's skip ends, or else the decoder's first
      frame. ADTS says nothing and keeps one AAC frame; an MP4 without an edit list
      keeps the encoder's delay, as FFmpeg's CLI does: such a file says nothing;
    - at the open the last packet is decoded and the length cut to what the decoder
      gives: a healthy file does not end in an error.
    - tables can give the first packet more than the decoder has (Opus in MP4 1008
      against 960, IAMF 15872 against 8192): the audio runs on, the stamps behind it
      are corrected at the open;
    - only ADTS and fragmented MP4 read every packet at the open: an MP4 reads its
      first packets and, from a little before the end, its last.
    - a change of channels, sample format or rate inside a file is damage: the read
      stops with an error there, as for MP3. A sample format the reader does not
      convert is refused at the open.
  - **Other formats**: FFmpeg decodes APE, TTA and WMA; they come only when users ask
    (Remon, 2026-11-01). FLAC stays with libsndfile, measured through both readers
    on 17 files: the audio is bit exact, and FFmpeg gains only 32 bit FLAC. It reads
    the whole file at the open or cannot seek reliably without a seektable, which
    libsndfile's FLAC does not write, and refuses the 8 bit FLAC libsndfile writes.
    MP3 stays with libmpg123, measured through both readers on 63 files: normal audio
    the same and no gain in speed, but FFmpeg goes on silently over damage, which our
    T73 rules report, and keeps the decoder delay where no tag names it.
- **T88 `can_read()` opens an MP4** (low; Fable, 2026-11-01): `avformat_open_input` for
  every MP4, twice on the `can_read()` path, on the GUI thread; a fragmented one is
  walked whole. The signature route could answer from the signature.
- **T89 FFmpeg reader: more reasons** (low; Fable, 2026-11-01): "no audio in it" also
  means that no packet came (`nero-chapters.m4b`); `truncated-64bit.mp4` says "cannot
  seek to frame 0"; FFmpeg's bare text at the open; xHE-AAC on FFmpeg 8 says "the file
  does not say enough" where the decoder refuses it.
- **T91 FFmpeg reader: what the third review left** (low; Fable, 2026-11-02): ADTS behind
  a tag of 1 MiB or more is refused, FFmpeg's probe stops there; the tail read of
  `mp4_length()` loses a read error; a file of which no packet decodes at the open gets
  "the format changes"; `test_ffmpeg_refusals()` skips without a word;
  `avformat_find_stream_info()` runs before the discard, so a video is read up to the
  probe size.
- **T86 A fragmented MP4 cut before the open** (low; Fable, 2026-11-01): it reads as
  a shorter healthy file, no error: the file has no length of its own. Document the
  limit or find one.
- **T76 Decoder leftovers** (low; Remon, 2026-10-31): FAAD's `m_openFailure`, set
  deep in its parsers where its functions could return the reason, and the opener
  every decoder carries while only WavPack's correction file needs it.

From the platform header (`src/support/TPlatform.h`, 2026-10-30):
- **T72 *BSD: allow it or not** (low; Remon, 2026-10-30). `TPlatform.h` stops the
  build unless `Q_OS_LINUX`, `Q_OS_MACOS` or `Q_OS_WIN` is defined, so FreeBSD,
  OpenBSD and NetBSD are a build error now. Qt gives them `Q_OS_FREEBSD` and
  so on plus `Q_OS_UNIX`, not `Q_OS_LINUX`. Not checked: whether Traverso ever
  built or worked there.
  - **To decide**: keep the three platforms, or test
    `!Q_OS_UNIX && !Q_OS_WIN` (macOS is `Q_OS_UNIX` too), which still catches
    a missing platform detection.
  - **If allowed**: the `Q_OS_LINUX` branches fall away there without a word
    (`TAudioDeviceThread.cpp`, `TProject.cpp`, `Main.cpp`, among others), and
    ALSA does not belong there; none of it can be built on this machine.

## Plans

- **T58 Remove the singletons** (Remon, 2026-10-28; before T60).
  - Safety net first: a headless test with the Dummy driver (start, add a
    client, cycles, remove, switch driver, shut down). **Done 2026-10-28**
    (`test_audio_device`, merged `73dad650`).
  - `audiodevice()` first: map every caller per lib and thread;
    Traverso.cpp owns the device (made after `tsmp()`, before the
    configuration is read; gone after the projects and the window, before
    the static `pm()` and `tsmp()`), `audiodevice()` a temporary
    bridge; replace callers bottom up (drivers: `m_device`; real-time code:
    rate and buffer size from `TProcessContext`, which removes the
    `T_BLOCKING_BY_DESIGN` exception; core and GUI: a reference through
    constructors); then the bridge goes.
  - `tsmp()` after it, by composition: the device owns its queue. Map
    first who uses it directly and through `TRtTransaction`/`TRtList`; a
    queue per device also gives tests a queue they apply on demand. Then a
    callback the audio thread posted that captures the device (`xrun()`'s
    `bufferUnderRun`) can no longer run after the device is gone: the
    queue's worker is joined in the device's destructor (Fable's review of
    `device-ownership`, 2026-10-28).
- **T59 TAudioDevice split** (later, each step isolated).
  - `TAudioRuntime`: the control interface (not the GUI interface:
    headless, agent and tests use it), owned by Traverso.cpp; owns the
    cycle, the current driver, setups, watchdog, xrun storm, setup
    messages, the queue.
  - `TAudioDriver`: the backend only, a narrow host interface instead of
    friend access, one `setup(const TAudioDeviceSetup&)`, a factory in one
    place; owns its channels and, for blocking drivers, its thread.
  - The cycle as its own class: clients, process context, posted changes,
    cycle count, CPU time (atomics), a post-cycle hook
    (`TRealTimeCallBack`, for the scheduler tick and later the published
    clock). Testable with the Dummy driver.
  - Transport sync as a driver capability (`TTransportSync`), not in the
    base: JACK follows and controls; PipeWire can follow natively
    (`spa_io_position`), control through pipewire-jack (to verify).
    Traverso's own transport is the sheet's, not the driver's (T35).
  - For headless tests only the singleton removal (T58) and the cycle are
    on the critical path; the rest are separate tasks.
- **T60 IO scheduler step 2: on hold until the singletons are gone (T58).**
  Design changes written 2026-10-28 (to go into `diskio-app-wide.md`):
  - the scheduler reads groups, never writes them (no friend access; the
    seek target in `StepInfo`, the transport written by the audio thread
    only);
  - `TIOHost` in ioscheduler (groups, streams, idle work), the one
    interface core sees, also the scheduler seam for headless tests;
  - the scheduler owned by Traverso.cpp, the tick through the post-cycle
    hook;
  - a group is one transport of a song (comments and names now; the
    transport owns its streams, `multi-transport.md` decisions 1 and 4).
- **T61 Design assignment: one scheduler, disk slots apart from threads.**
  Check `dispatcher-conclusie.md` (which disk, which scenario, CPU or
  disk), measure overdubbing on an HDD with one and two schedulers. Later:
  a prefetch phase holds the disk slot, the decode runs without it.

## Decisions taken (Remon, 2026-10-28)

- **Project rate** is the user's wish, also when the hardware does not run
  at it. New project: the device's rate by default; an existing project's
  rate never changes silently (show both rates when they differ).
  Resampling stays at the source side (per stream, IO threads): no cost to
  the round trip. Recording at the project rate by default (the writer
  resamples on the IO thread, best quality), an option for the hardware's
  rate. Converting on import is an optional optimisation that keeps the
  original. T5 and T7 first.
- **A second output device** (live playhead): left to the sound server
  (PipeWire routing, JACK via zita, macOS aggregate devices, on Windows
  ASIO4ALL or Voicemeeter). Without one: the live device is the master
  clock, the editor output a slave output driver with a ring and an
  adaptive resampler on the slave's side; no resampling on the live
  output. One audio thread processes both transports; open: plugins with
  two transports (an instance per transport, or main transport only).

## Decisions waiting for Remon

- Scenario decoder O1: the wanted core behaviour per fault (after step 3).
- Branch 3: the questions in §10 of `input-controllers.md`.
- Shutdown: an explicit `shutdown()` or the order in the destructors (as
  built)? **(check: still open)**
- Flush-and-wait on the schedulers before saving a project: a requirement?
- Disk-load meter next to or instead of the CPU meter?
- Upgrade path for old projects after the timebase change: before a release.

## Parked / reference

- **Multi-transport** (`doc/design/multi-transport.md`, Ben's plan): reference
  only, not a leading design. Ask before a design choice that makes it
  significantly harder.
- **Gain ramp per transport**: the ramp state belongs to the transport
  (multi-transport §9.3), not to the track.
- **Multichannel playback**: clips with more than 2 channels read and draw,
  playback plays the first 2 (`7cc72041`); idea: a channel mapping onto mono
  or stereo.
- **Curve GPU benchmark** (`curve-gpu-bench`): kept as reference until the new
  canvas proves itself, then merge its docs and delete it.
- **Processing-node graph**: one root snapshot per batch, clips without
  automation not publishing; not now (load cost harmless).

## Documents

- **In the repo, `doc/design/`:** `diskio-app-wide.md`, `diskio-rounds.md`,
  `peak-records.md`, `waveform-grid.md`, `recording-latency.md`,
  `recording-session.md`, `recording-data-safety.md`, `multi-transport.md`,
  `curves-and-cursors.md`, `input-and-viewport-flow.md`,
  `readahead-investigation.md`; older peak notes (`peak-redesign.md`,
  `peak-diskio.md`, `peaks-v6-report.md`).
- **Next to the repository (`git/`), still to be brought in or retired (learning
  plan step 1):** `actions-and-controllers.md`, `design-principles.md`,
  `realtime-audio-path.md` (old copy; the repo has the current one), the
  scenario-decoder work orders, `fable-review-2026-10-25.md`,
  `fable-review-2-2026-10-25.md`, `leerplan-ontwerpproces.md`.
