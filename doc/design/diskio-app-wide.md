<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# One DiskIO for the application (design, 2026-10-21)

Worked out with the user on 2026-10-21 under the KISS principle, after the
peak redesign. Steps 1 to 5 of section 10 are built (the last two on branch
`app-scheduler`, 2026-10-22); the later steps are not. Importing and merging
through the scheduler is a work order of its own.

**This is the one design document of the IO scheduler** (since 2026-10-22).
How it works now, thread by thread, is in `diskio-rounds.md` (as built). It
replaces, for their designs and decisions, the work orders
`ioscheduler-taak.md` (and its first version), `dispatcher-conclusie.md` and
`diskio-uniforme-taken.md`; they are done or replaced and are not updated
any more. A decision made in a work order comes back here.

## 0. Requirements

**Concept of 2026-10-22: Remon confirms them row by row.** A requirement is
written as "X never happens, also when Y". [hard]: must hold; [pref]: should
hold, may be traded with a reason; [idea]: wanted, not yet decided. A change
stays visible: the old text struck through, with the date and the reason.

| # | Requirement | Kind | Proven by | Status |
|---|---|---|---|---|
| R1 | Recorded audio never waits behind a read, also when a read is slow or a disk spins up | hard | two schedulers with pools of their own (§10); no test yet | concept |
| R2 | The audio thread never waits for a scheduler without a timeout: when it waits (a late buffer, freewheeling) a timeout lets it go on. On an architecture without lock-free atomics it may block on an atomic (accepted) | hard | review: atomics and the wake only (`diskio-rounds.md` §2); the freewheel wait has a limit of 1 s | decided 2026-10-23 |
| R3 | A song that plays never runs dry because of idle work, also when every idle task wants all threads | hard | `io_scheduler`: idle work on two of three threads, a stream gets its steps; `io_planner`: hysteresis | concept |
| R4 | A seek of a song that plays never waits behind refills or idle work, also when long idle steps run | hard | `io_planner`: order; `io_scheduler`: a seek next to 100 ms idle steps under 1 ms | concept |
| R5 | No idle work runs while any song records, also when another song plays or seeks | hard | `io_planner` and `io_scheduler`: no idle step while recording | concept |
| R6 | A seek of one song never moves the streams of another song | hard | `io_scheduler`: two songs | concept |
| R7 | A stream gets no step after `remove_stream()` returned, also when its step ran at that moment | hard | `io_scheduler`: removal while steps run | concept |
| R8 | A stream that cannot make progress (end of its file, out of its window, an error) never spins | hard | the rule of one refill step per tick; no test of its own yet | concept |
| R9 | A new rate or buffer size never reaches a stream outside its own seek, and a new resampler never takes a rate the stream does not read at yet | hard | `io_scheduler`: rate per stream, resampler at the stream's rate | concept |
| R10 | At most one seek per song at a time, also for a JACK relocation | hard | debug assert in `TIOGroup::request_seek()` | concept |
| R11 | The GUI never writes what a step or the scheduler reads at the same time | hard | **violated**: a reader's position and range (§13, point 1) | concept |
| R12 | The schedulers' threads and pools never end in a static destructor, and never before the last project went | hard | the smoke run's exit; no test | concept |
| R13 | No step waits for a slower step: the work goes at about all work ÷ threads | pref | `io_scheduler`: a fast task before a slow one; `traverso_measure_idle_builds` | concept |
| R14 | A removal waits only for its own stream | pref | `io_scheduler`: 200 removals next to 50 ms steps | concept |
| R15 | On a disk that seeks at most one idle read at a time; on an SSD as many as there are idle threads; the setting overrides | pref | `io_planner`: idle limit; `idle_limit_for()` by hand | concept |
| R16 | The seek of a song that does not play is a preparation: idle work, never before refills | pref | `io_scheduler`: an idle seek one stream at a time | concept |
| R17 | Nothing is timed for the plan: the kernel schedules, the scheduler only knows the order | pref | review | concept |
| ~~R18~~ | ~~Recorded audio survives a stall of the disk of N seconds~~ | — | deferred 2026-10-23 until a stall is observed (the size of the write rings, §12) | deferred |
| R19 | The audio thread never allocates on the heap, also not to log (no QString there; `TLOG_RT_*` takes numbers and literals) | hard | review | decided 2026-10-23 |
| R20 | The code builds as C++20: libc++ on macOS has no `std::move_only_function` | hard | `std::move_only_function` in `TAudioThreadMessageQueue.h` is the only C++23 use (with `std::function` the whole tree builds as C++20, GCC and Clang, 2026-10-23); Ben's patch for macOS, to come on GitHub, takes care of it | decided 2026-10-23 |
| R21 | Positions and block numbers are 64 bit, also on 32-bit architectures | hard | review | decided 2026-10-23 |
| R22 | The consumer (the audio thread) decides what is true (position, direction, the slots it holds); the producer (the reader) predicts from the same region list, and a wrong prediction costs read work, never wrong audio; the audio thread does constant, allocation-free bookkeeping only | hard | review; every slot carries its file position | decided 2026-10-23 |
| R23 | Every slot is owned by exactly one thread at a time | hard | review: slots move through the SPSC queues | decided 2026-10-23 |
| R24 | Recording writes forward: the write side stays a FIFO | hard | review | decided 2026-10-23 |
| R25 | Near misses and worst cases are reported per session, scheduler efficiency apart from hardware latency, in numbers that stay comparable across buffer designs | hard | the instrumentation (`IO Scheduler Instrumentation and Buffer Design.md`, session 2026-10-23); not built yet | decided 2026-10-23 |
| R26 | Playback that jumps at region boundaries, and freewheeling (the audio thread faster than real time, waiting with a long timeout) keep working | hard | not built yet (regions); freewheeling as today | decided 2026-10-23 |
| R27 | Backward playback and scrubbing; a new seek while one runs: the last asked for wins, what is left of the old one lapses, a read that runs finishes | idea | research, after the random-access buffer (§13) | decided 2026-10-23 |
| R28 | Cheap seeks and loops that reuse what is read; buffer sizes per device, growing for slow media (a NAS) | idea | after the instrumentation | decided 2026-10-23 |

## 1. Why

(The library is named `traversoioscheduler` since 2026-10-21, sections 8 and
10; "DiskIO" below is the scheduler.)

Before (until 2026-10-22):
- every sheet had two DiskIO threads (one read, one wrote);
- every DiskIO ran its rounds on the one global thread pool
  (`QtConcurrent::blockingMap`);
- importing and merging have threads of their own (`TAudioFileCopyConvert`,
  `TAudioFileMerger`, both a `QThread`), still today.

The threads that did the work were shared, but nobody decided which stream
came first across them: every DiskIO planned only its own streams. One
scheduler for the whole application that reads and one that writes, each
with a pool of its own, lets one planner decide. Recorded audio still never
waits behind a slow read (R1): writing keeps a scheduler of its own.

## 2. The concept: an album of songs

A project is an album and its sheets are songs. Who plays, and in which
time, is a choice of the project:

| Mode | What plays | Each song's time |
|---|---|---|
| Song | the active song | its own |
| Album | every song, one after another | album time minus the song's offset |
| All at once | every song | the same position for all |
| Independent (chosen on purpose) | the songs that were started | each its own transport |

DiskIO knows none of these modes. They are the project's policy: it sets
the transports of the songs.

## 3. A group per song

A song is a group in DiskIO: its transport location, its seek (request and
location), whether it rolls, and later its play list (section 5). This is
the state every sheet has today, moved out of its own DiskIO threads into
the shared one. A stream belongs to a group.

The order (decided by the user on 2026-10-22, replacing "refills before
seeks"):
1. **The seeks of songs that play**, before all refills. A song plays when
   it rolls, or will roll once its seek is done (a seek during playback,
   play pressed during a seek).
2. **Refills**, the emptiest first (a writer's fill is its free part: the
   fullest queue first).
3. **Idle work** on the threads that are left, within the idle limit,
   behind the hysteresis (only the readers of songs that play count) and
   off while any song records:
   - the seeks of songs that do not play: a preparation to play (on a disk
     that seeks, one stream at a time);
   - the readers of parked songs (section 4);
   - peak post-processing and peak builds: the heaviest task of all (the
     most frames) never waits, the others take their turn in the order they
     came (decided 2026-10-23); a build reads a second of its source per
     step;
   - imports and merges (the next work order).

`TIOPlanner` has per stream whether it seeks and whether its song plays
(built 2026-10-22; parked follows with section 4). It stays pure and tested
headless.

The audio thread wakes the schedulers once per cycle, from the project, not
once per sheet (built 2026-10-22).

## 4. Parked songs

A song that does not play keeps its readers full at its own position, as
idle work. Once its buffers are full it asks for nothing, as nobody
consumes. Switching to another song is instant, as it is today with a
DiskIO per sheet: the new song's readers are ready where it stood, and the
project drives that song's transport from there.

## 5. Play lists: loops and regions

Later, for playback only: a loop range, or jumps from one region to the next
(a composer compares bars 3–5 and 9–12). A play list is an ordered list of
time ranges, with a loop over the list or not. It belongs to the group,
published as an immutable snapshot like a curve (`TRtValue`). All clips
inside the ranges play. A loop range is a play list of one range.

The reader changes in two places only, and DiskIO not at all:
- **The next slot.** Every slot already holds its transport and file
  location; the next one is now "the last plus one slot"
  (`TBufferedAudioStreamReader::process_realtime_buffers()`). It becomes "what
  the play list has after the last". A slot holds frames in play order, so
  across a jump the ring already has the next audible second: no seek, no
  gap. A slot across a jump is filled from two reads.
- **The window.** `transport_in_window()` measures the distance in linear
  time now ("about one to four seconds"). It becomes the distance along the
  play order, so a clip in bars 9–12 prefills when the transport nears bar 5.

What else it needs:
- the audio cycle splits at a jump for what depends on time: curves and
  automation, fades, the playhead, the time information plugins get;
- a fade of a few milliseconds at a jump against clicks, by the reader,
  which knows where the jump is;
- a play list that changes while it plays (a loop end dragged) is a seek of
  that group to where it is;
- a minimum length of a range, at least an audio buffer;
- bars need a tempo map, which Traverso does not have yet; the play list
  itself is time ranges.

## 6. Punch in/out and loop recording

The same idea for recording, later. Playback runs on, and recording happens
only inside the punch ranges. The audio thread knows the transport and
starts and stops at the edges, to the sample. The writer writes what it
gets; with a loop, a new take per pass. DiskIO needs nothing for it, so
this design does not stand in its way.

## 7. JACK transport

JACK has one transport. It drives the active song (decided: a JACK session
is a song); driving all songs is an option of the project's policy. Today
`TProject::transport_control()` passes it to every sheet.

## 8. Where it lives

A library `traversoioscheduler` in `src/ioscheduler` (decided 2026-10-21):
`TIOScheduler` (formerly `TDiskIOThread`), `TIOPlanner`, `TIOIdleTask` and the
stream interface `TIOStream`, linking `traversosupport` and Qt Core only.
The readers and writers in core implement `TIOStream`; their decode buffer
is `thread_local` in core. The name says what it does: it plans and hands
out work, the reading, decoding, encoding and writing are the streams'.

It does not go into `audioruntime`, which is the real-time side (drivers,
device, buses, RT primitives).

## 9. Disadvantages, from a DAW's point of view

1. **Memory.** A reader holds a ring of one second, about 384 KB of stereo
   at 48 kHz. A song of 175 clips is about 67 MB, and an album of twelve such
   songs about 800 MB. That is so today too: every sheet prepares the rings
   of all its clips. Later options:
   - a parked song gives its rings up, and a switch costs a seek;
   - a reader out of its window makes its ring only when the transport
     comes near.
2. **A seek beside playback** (independent mode): on a hard disk it competes
   with the refills of the song that is audible. The order of section 3
   protects that song, but two songs at once stay tight on a hard disk.
3. **Bandwidth:** more rolling songs, more streams. In album mode one song
   plays at a time but for the transitions.
4. **Gapless transitions and crossfades between songs:** groups with offsets
   allow overlap. Whether a transition has no gap depends on how far ahead
   the readers read, the open point "clip-edges fix A"
   (doc/design/readahead-investigation.md).
5. **Alignment to the sample** in album and all-at-once mode: every group
   moves on in the same audio cycle, so they stay aligned as long as the
   project starts them in the same cycle.
6. **Independent mode mixes the songs into the master:** two songs at once
   are heard together. There is a precedent: Reaper's "play background
   projects". It is a niche.
7. **The current fill meter and DiskIO CPU time per sheet** become one
   number for the application.

## 10. Steps

The work order, "Werkopdracht: van DiskIO naar TIOScheduler" (version 2,
2026-10-21), holds the steps that pay off now:
1. the peaks library `traversopeaks`;
2. the library `traversoioscheduler` and `TIOStream`: moving code, no
   behaviour change;
3. the scheduler has a thread instead of being one: `dispatch()` testable
   without a driver, one wake semaphore, synchronous removal, the shutdown
   order in one place;
4. one scheduler for the application, still on rounds (step A of
   `dispatcher-conclusie.md`, built 2026-10-22 on branch `app-scheduler`):
   two instances, one that reads (and runs the idle work) and one that
   writes, owned by `TProjectManager` and ended by `~Traverso`
   (`stop_schedulers()`, after the last project went), with pools of their
   own (read: half the cores plus one, the reserve for refills of step 5;
   write: two; at least two), a group per sheet (`TIOGroup`:
   transport, seek, plays, records, idle limit), the order above, one wake
   per cycle from the project, idle work off while any song records, the
   idle limit the lowest of the songs', the meters one number;
5. a dispatcher without the round's barrier (step B): every step goes to
   the pool on its own as soon as a thread is free, and reports back when it
   is done; the next one starts at once.

The reason for step 5 is not slow media but the work per step, which
differs: in a round every task gets one step and the round ends with the
slowest, so a WAV peak build ran exactly as fast as a WavPack one next to
it, and all imported work goes at the pace of its slowest step. Measured
before step 5 (`traverso_measure_idle_builds`, 54 files of six formats and
three rates, 6 threads): 1.75 s, 58% of the threads used.

Later, each a step of its own:
- JACK drives the active song, and starting another song stops the one that
  plays (decided, section 11);
- parked groups as idle work;
- importing and merging as scheduler tasks, with their peaks in the same
  step (the next work order);
- play lists, and later punch in/out;
- album mode and the project's mode policy.

Each step with headless tests: on the planner (the order of section 3, the
flags), on the scheduler with fake streams, and on the reader's play-order
functions (pure).

From the work order's "Later" (`ioscheduler-taak.md`, 2026-10-21), each a
step of its own:
- **A recording session as a class of its own** (`TRecordingSession`,
  decided 2026-10-21): it owns the writers of one recording; the recording
  logic goes out of `TSheet` into it (`prepare_recording()`, the recording
  clips, `clip_finished_recording()`, idle work off and on, the autosave at
  the end, the takes of branch `recording-session`). A permanent member of
  the sheet with two verbs, `start_takes()` and `end_takes()`; a seek during
  a recording is `end_takes()` plus `start_takes()` in `seek_finished()`. One
  concrete class, no new behaviour: the code moves over mostly as it is.
  Order: the runtime test and merge of `recording-session`, then this step.
  At that merge: discarded takes keep their readers until the project
  closes (32a2c2ca) because removal was asynchronous then; with synchronous
  removal that reason is gone, but freeing at once still waits until the
  audio thread processed the clip's removal.
- **Slow media** and **planning per disk**: in section 12 (deferred).

## 11. Decided

Decided by the user on 2026-10-21:
- **DiskIO gets a thread pool of its own**, so the priority is DiskIO's
  alone. The global pool stays for the cache's eviction and the like. The
  pool size that `main()` sets on the global pool (branch `peak-records`,
  half the cores) then goes to DiskIO's pool, and the global pool keeps
  Qt's default. (Built 2026-10-22 as half the cores plus one: section 10.)
- **Starting another song stops the song that plays.** Independent playback
  is a mode of its own, chosen on purpose.

- **JACK transport drives the active song by default.** Driving all songs
  is an option of the project's policy. (Built after the app-wide
  scheduler, section 10.)
- **The library is `traversoioscheduler`** (section 8).

Decided by the user on 2026-10-22 (`dispatcher-conclusie.md`, which
replaces phases 3 and 4 of the work order):
- **The round loses its barrier** (section 10, step 5), for both
  schedulers: writing can be CPU-bound too, into a compressed format. This
  replaces "the round keeps its barrier for now".
- **A seek of a song that plays goes before all refills; the seek of a
  song that does not play is idle work** (section 3).
- **`TProjectManager` owns the two schedulers.**

Decided by the user on 2026-10-23, after measuring on "Minidisk opname"
(52 builds, one of them 22 minutes, built last and alone):
- **The heaviest idle task of all never waits; the others go in the order
  they came.** Measured against the heaviest first for all threads (all
  built 8.4–9.7 s but the median build at 7.3–8.3 s) and the order they
  came (median 3.4–3.6 s but all built 12–15 s, depending on when the long
  file starts): all built 8.6–10.9 s, the median at 4.0–4.7 s.
  A stop-gap (2026-10-24): the long file is to be built by several readers
  at once, each its own idle task on its own core, for compressed files
  only (a WAV is read faster than it is reduced). The weight goes with
  that; a task for later (T19, `peak-records.md` section 7).
- **A peak build reads a second of its source per step** (was a fifth):
  five times fewer hand-overs to the pool.

Decided by the user on 2026-10-23, on the session document `IO Scheduler
Instrumentation and Buffer Design.md` (requirements R19 to R28 in §0):
- **First the review, the runtime test and the merge of the open branches**
  (`resample-reader`, `resampler-choice`, `app-scheduler`); then the
  instrumentation, without a change in behaviour, as the baseline; then the
  random-access slot buffer.
- **C++20, not C++23** (R20): libc++ on macOS has no
  `std::move_only_function`. Ben's patch, to come on GitHub, takes care of
  it; not a task of this work order.
- **An architecture without lock-free atomics may block the audio thread**
  on an atomic; positions and block numbers are 64 bit (R2, R21). The
  `static_assert` on lock-free 64-bit atomics in `TRtTransportPosition.h`
  follows this decision when it is touched next.
- **The audio thread waits only with a timeout** (R2).
- **No heap allocation on the audio thread**, also not for logging (R19).
- **Slow media get a dynamic buffer** (R28) instead of steering the design:
  the instrumentation and the devices are the scheduler's.

## 12. Deferred

Until a measurement asks for it:

| What | Why deferred |
|---|---|
| A count per device (a device that hangs does not hold all threads) | Devices that hang are rare locally |
| `QStorageInfo` as a device id, recognising network file systems | Belongs to the count per device |
| `remove_stream()` without waiting, a deadline at the end | Only for steps that never come back |
| Detecting spinning disks on Windows and macOS | Only faster builds on an SSD; the setting does |
| One loop with two pools instead of two instances | Only when load on one hard disk becomes a measured problem |
| Group removal in one request | With the dispatcher a removal waits only for its own stream |
| The size of the write rings (R18) | Only when a stall of the disk is observed (decided 2026-10-23) |

## 13. Open points

From the review of branch `app-scheduler` (2026-10-22, `status.md`), with
what is to be done:
Status 2026-10-23: point 3 was wrong as written: after a seek a step gives
-1 mostly for a reader out of its window, which is normal, and the warning
of 44029a56 fired for each of them (54 at every load of "Minidisk
opname"). The stream itself now warns, only when its seek did not get in
sync for another reason. Status 2026-10-22: 2, 3, 5 and 6 are done (commits "assert that a song's
transport does not move while it seeks", "a seek step that fails says so",
"no rounds in names and comments", and this document's corrections); 1 has
a design proposal; 4 waits for the decision on a disk-load meter. Checked
and nothing to do: `seekFinished(group)` is queued with the sheet as the
connection's context, and the group is the sheet's, so it cannot go before
the signal arrives.

1. **The GUI writes a reader's position and range in place**
   (`m_streamStart`, `m_streamEnd`, `m_sourceStartLocation`) while the
   scheduler thread and the workers read them: an open race (R11). A design
   proposal first (below, not decided).
2. **Two writers on a group's transport location**: the audio thread every
   cycle the song rolls, the scheduler at the start of a seek. It holds
   because a song does not roll while it seeks; an assert is to say so.
3. **A seek step that fails** (`step()` returns −1) counts as done without
   a line in the log.
4. **The CPU meter** (the time the steps took ÷ the wall time) can be above
   100% and counts the time a step waits for the disk. A disk-load meter is
   a decision (below).
5. Clean-up: `Qt6::Concurrent` is no longer used; comments that still speak
   of rounds.
6. The design documents disagree in places (this one and `diskio-rounds.md`).

**Proposal for point 1 (2026-10-22, not decided).** Who touches the three
values:
- the GUI writes them: `TAudioClip::set_source_start_location()` (a trim,
  a split) and `TAudioClip::sync_location_to_stream()` (on every
  `locationChanged`), in two separate calls;
- the scheduler thread reads the window in `disk_state()`
  (`transport_in_window()`); a step reads all three in
  `rb_seek_to_transport_location()` and `process_realtime_buffers()`, and
  labels every slot it fills with the file location it read;
- the audio thread does not read them: it asks for the file location it
  computes from the clip's own values, and a slot whose label does not
  match sends the reader out of sync, so the next step seeks again.

Because of that label, a step that sees a mix of old and new values never
makes the audio thread play wrong audio: either a slot carries exactly the
file location asked for, or the reader resyncs (as after every clip move
anyway). What has to go is the data race itself, and a mix within one
function (a slot labelled with one value and read with another). Proposed,
the simplest that does it:
- the three values become `std::atomic<qint64>` (universal frames,
  relaxed), as the group's transport is;
- every function of the disk side loads them once at its start into
  locals and uses only those, so a slot's label and what is read for it
  come from the same values;
- the GUI's two setters stay as they are; the resync covers a step that
  falls between them.

Rejected: an immutable snapshot of the three behind an atomic pointer (who
frees the old one while a step reads it?), and a pending window under a lock
taken at the start of each disk access (it makes the three consistent, but
the GUI sets them in two calls anyway, and the resync is needed regardless).
Not part of it: the clip's own values (`m_location`, `m_sourceStartLocation`,
`m_length`) are read on the audio thread while the GUI writes them, and a
recording's `m_length` is written on the audio thread while a save reads it:
the same kind of race on the audio side, for the RT transaction design.

**Found 2026-10-22: the shutdown order is not in one place.** On a normal
quit (the window's close, the Quit action, the last window closed) it is
spread over five places: `TProjectManager::exit()` and
`set_current_project(nullptr)` (save, cancel the peak builds, drop the
views, `TProject::set_project_closed()`, which waits up to a second until
the audio device let go of the project); Qt's clean-up when `exec()`
returns (the project's `deleteLater()`: `~TProject` removes the clips' and
sheets' streams and groups); `main()` (`delete traverso`); `~Traverso`
(`stop_schedulers()`, `audiodevice().shutdown()`, the main window, the
themer, `config().save()`); and the static destructors (`pm()`,
`audiodevice()`, the peak cache's state). On SIGINT (`catch_signal()` in
`Main.cpp`) `Traverso::shutdown()` calls `pm().exit()` and then `exit(0)`
in the signal handler: the project is saved but never deleted, the
schedulers never stop, the main window is not deleted, `config().save()`
does not run, and the audio device stops in its static destructor. That
runs inside a signal handler (Qt calls, locks) and possibly on another
thread than the GUI's. If the audio device does not let go of a project in
time, the project is not deleted either, and `stop_schedulers()` meets
streams that are still registered (an assert in a debug build).

**Found 2026-10-22: a save while a recording finishes its file.** Between
the stop (`TAudioClip::finish_recording()`) and
`TAudioClip::finish_write_source()` the write scheduler writes what is left
in the takes' rings and closes the files; that is usually one or two
cycles, longer on a slow disk. A save in that time is allowed
(`TProject::is_recording()` is false once the transport stopped, and
`TProjectManager::save_project()` does not ask) and writes each take as it
is then: the clip with its whole recorded length, its source with length 0
(the source has no file open yet). The autosave at the end
(`TSheet::clip_finished_recording()`) writes the final state over it, so
when the application goes on nothing is lost. When the project closes in
that time (quit right after the stop: `is_save_to_close()` is true), the
save is followed by `~TAudioClip`, which takes the writer out
(`remove_stream()` → `retire()` frees the ring with the slots not written
yet); the writer's destructor closes the file with what was written. The
take's last part is lost, and the saved clip is longer than its file. The
branches `recording-latency` and `recording-session` change how the length
grows, not this.

Not done yet from the work orders:
- the measurement of 32 WavPack tracks, which decides the size of the
  write pool (two for now);
- the size of the write rings: as long as the stall to survive (R18); about
  one second now; 32 mono tracks at 48 kHz float cost about 6 MB per second
  of ring.

Decisions waiting for Remon:
- the shutdown: an explicit `shutdown()` (`design-principles.md` §6) or the
  order in the destructors, as built;
- flush and wait on the schedulers before a project is saved: a
  requirement or not;
- a disk-load meter (required ÷ achieved throughput) next to the CPU meter,
  or instead of it.

Research (2026-10-23):
- **Scrubbing and a seek while one runs** (R27): the last asked for wins.
  A read step that runs is not broken off (that gains little); what is
  left of the old seek lapses: the streams that did not seek yet seek to
  the new location, those that did seek again, one whose step runs after
  it. With the random-access buffer first. Today a seek during a seek is
  refused by the sheet (`TSheet::initiate_seek_start()`, and R10). In the
  dispatcher it would mean: the group's seek takes the new location, all
  its streams seek (again), and a report of a step of the old seek must
  not count for the new one (a seek number per group in the step's
  information).
- **Stream buffers the size of the audio thread's buffers**, so a period
  is one copy. Found: they are so today (a slot is one period of
  `bufferSize` frames, the ring `outputRate / bufferSize` slots, one copy
  per channel and period), and that is where the ring rebuild on a buffer
  size change comes from, a slot that is longer than the clip it holds at a
  clip's edge, and a block numbering that would change with the period. A
  slot of its own size (a period across at most two slots: two copies)
  keeps block numbers stable for the random-access buffer, lets the decoder
  read in bigger pieces and lets freewheeling use much bigger periods
  without touching the ring. Proposal: do not tie them together.

Before the merge of `app-scheduler`: the runtime test with recording.
