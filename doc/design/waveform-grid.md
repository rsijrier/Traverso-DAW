<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Waveform columns on a power-of-two grid (design, 2026-10-24)

The user's work document "power-of-two waveform grid", with the review's
changes. A raster view asks for one column per device pixel; from more than
32 frames per pixel on those columns come from a grid of 2^s source frames,
aligned in source time. On that grid the existing query visits about one
record per column: an aligned range has no odd edges in `visit_range()` below
the top level. No new query code; painting is unchanged and moves to the GPU
later.

## 1. The grid: `TWaveformGrid`

- Column k covers the source frames `[k << s, (k + 1) << s)`; s is the
  smallest power of two at or above the frames per pixel.
- **From one level-0 block per column on** (`MinShift` = 6, more than 32
  frames per pixel). Below it a query reads the samples, which costs the same
  on columns of any width: a grid there only cost time and changed the look
  (the work document had one frame per pixel; measured, that was up to 12%
  slower at 1 to 32 frames per pixel).
- **Exact edges.** Start and width are built from frames
  (`TTimeRef(frames, rate)`). There is no grid where 2^s frames are not a
  whole number of universal frames: 37.8 and 50 kHz never, 384 kHz below 2
  frames and 768 kHz below 4 frames per column (both below `MinShift`).
- **`TTimeRef(frames, rate)` is exact at every rate** since this work
  (`TTimeRef: frames to time exact`): it divided first and was up to 0.7% off
  (25 s per hour at 384 kHz). It now rounds the rest up, so
  `frame_at_or_before()` gives the frame back at any rate, and is unchanged
  for every rate that divides the universal rate.

## 2. Device columns: `TWaveformColumns::device_columns()`

- **A pixel shows the grid column that holds its centre.** A grid column is
  then 1 to 2 pixels wide, every grid column in view shows in at least one
  pixel (no peak is lost), and a peak is at most a grid column (2 pixels)
  from its place.
- **Clip edges.** A grid column that reaches outside the clip's source range
  would show audio from outside the clip: its pixels (at most two per edge)
  come from the exact query, with the exact query's own boundaries. A clip
  that reaches the end of its source (a whole file, a recording) has no edge
  there: there is no audio past the end.
- **Cheap where it gains nothing.** The pixels step through the columns'
  starts by comparison, not a division per pixel (exact, because
  `TTimeRef(frame)` is the first universal frame of that frame). With one
  grid column per pixel (exactly 2^s frames per pixel) and no clip edge in
  view, the grid's columns go straight into the output.
- `TWaveform::device_columns()` serves a finished file, a build and a
  recording (the stateless read of `TPeakStream`); it replaces
  `TWaveform::columns()`, which had no other user.
- `TAudioClipView::draw_columns()` passes the clip's source range; the
  pixel loop, gain, RMS band and outline are unchanged.
  `update_recording()` keeps its 2 pixel margin, now documented: the grid
  column at the recording's edge grows with every append.

## 3. Measured

Release, one hour of 48 kHz stereo, a 4K row (3840 device columns), the
query only (`traverso_peak_benchmark`, best of three runs):

| Frames per pixel | Today (exact query) | Grid (`device_columns()`) | Change |
|---|---|---|---|
| 0.25, 1, 8 | 0.08, 0.19, 1.30 ms | the same (no grid) | -3% to 0% |
| 40 | 6.73 ms (samples) | 0.078 ms (level 0) | -99% |
| 63 | 10.5 ms (samples) | 0.116 ms | -99% |
| 64 | 0.111 ms | 0.112 ms | +1% |
| 100 | 0.113 ms | 0.090 ms | -20% |
| 256 | 0.145 ms | 0.110 ms | -24% |
| 1000 | 0.204 ms | 0.121 ms | -41% |
| 4096 | 0.287 ms | 0.125 ms | -56% |
| 30000 | 0.606 ms | 0.140 ms | -77% |
| 45000 (the hour) | 0.612 ms | 0.122 ms | -80% |
| 65536 | 0.118 ms | 0.119 ms | +1% |

At 40 and 63 frames per pixel the benchmark computes its samples with sin().
Through the real reader (`TAudioReader`, warm cache) the exact query
costs 2.15 and 3.16 ms for a WAV file and 6.8 and 10.7 ms for a FLAC file;
the grid 0.22 and 0.34 ms for both.

While a file is written (mapped per read): the whole hour 2.56–2.70 ms
against 2.00–2.09 ms, 10 minutes 0.64–0.67 ms against 0.43–0.45 ms; page
faults dominate there. A finished file, 10 minutes: 0.35 against 0.13 ms.
Headless app, seven projects: every build done, no warnings, a clean exit.

Code lines: the peak files (`src/waveform`, `TWaveform`) 1933 to 2041,
`TAudioClipView` 795 to 797, `TTimeRef` 435 to 444; tests 2440 to 2858.

## 4. Tests

`test_waveform_grid` measures the math:

- the shift chosen, the rates with a grid, exact edges at 44.1, 48, 96 and
  384 kHz;
- one record per column: the pyramid's other records are poisoned
  (saturated), so any other visit shows, also above the top level and in the
  chunk being written; a query one universal frame off fails on every level;
- about 420 random views per channel count (1 to 3), on a file being written
  and on a finished one: every pixel equals the grid column of its centre
  (computed in the test with a division of its own) or the exact column at a
  clip edge, bit for bit; the grid columns in view have no gap and are 1 or
  2 pixels wide; against the samples, every probed sample lies within the
  column shown by a pixel at most 2 pixels from its own.

Mutations caught: no clip edge, a grid one step too fine (gaps), one step too
coarse (wider than 2 pixels). `test_timeref` checks 16 rates, beyond 32 bits
of frames. ThreadSanitizer clean on the peak tests.

## 5. Visual check

Rendered with the library and the view's pixel loop (exact above, grid
below; a real recording, DPR 1.0, 1.25, 1.5 and 2.0, a clip edge in view):

- The envelope's shape and the peaks' height are the same at every zoom; the
  grid's edges are blockier by 1 to 2 device pixels, visible enlarged.
- The clip edge is exact: no audio from before the clip.
- From 33 to 64 frames per pixel the RMS band appears, where the samples
  showed none (level 0 instead of the samples).

Open for Remon, in the app: the RMS band from 33 to 64 frames per pixel, the
look at fractional DPR, and zooming (detail changes in a step at each
doubling). If the band is not wanted there, `MinShift` 7 keeps the samples
up to 64 frames per pixel and gives up the largest gain.

## 6. Tile cache: report (not built)

The idea: the columns of 256 grid columns per tile, keyed by source,
peak-file generation, shift and tile, in an LRU of about 32 MB; a finished
file's tiles immutable, a growing file's tile final once its frames lie in
complete chunks.

**Repaints in today's view** (from the `update()` call sites in
`TAudioClipView`, not counted in a live session). During a recording a clip repaints strips at
the display rate (`update_recording()`), a few pixels wide, touching a few
pages; a whole repaint comes once a minute (the bounding rect grows) and
with the user's zoom, scroll, mute or lock. During a build a clip repaints
whole, at most 10 times a second (`m_buildRepaint`). The view's query of a
whole repaint, an hour in view, per stereo row: 0.12 ms for a finished file,
2.0 ms for a file being written (page faults); painting it costs 3 to 4 ms.

**What a cache would save.** On a finished file at most 0.12 ms of 3 to 4
ms. While a file is built with an hour in view, about 1.9 ms per whole
repaint, 10 times a second: about 2% of a core per clip being built, for as
long as the build runs (100 s for the 5-hour file of project Untitled; most
builds take seconds). Recording strips: nothing measurable.

**Complexity.** A tile store with LRU and byte budget (about 150 lines and as
many test lines, in the waveform library); the validity rules (final once in
complete chunks, the running chunk recomputed) and a generation (the stream
until published, then the file's size and time) about 50; `device_columns()`
taking its grid columns from tiles about 80. TWaveform exists per clip, so a
store shared by the clips of a source has to live outside it (a cache object
of the application, next to `TPeakCache`). In all about 300 to 400 lines and
200 test lines in `TWaveformColumns`, `TWaveform` and a new store.

**Carries over to the GPU view:** the key (`{shift, tile}` from the grid),
the validity rule and the budget; the GPU view keeps geometry per tile, fed
from the same columns. Specific to today's view: nothing that is worth it.

**Recommendation: postpone entirely.** In today's view the query is already
a small part of a repaint, and the only notable case (a long build in view)
is temporary. Build the tiles with the GPU view, on the grid's keys and the
complete-chunk rule.

## 7. Grid walk: measured and decided (2026-10-29)

From the work order "Waveform grid-walk prototype" (Remon, 2026-10-29). On
the grid an aligned column is one record on level `shift - 6`, so
`device_columns()` of a finished file walks those records directly
(`TWaveformColumns::walk_grid()`): all channels from one record, min and max
through the code tables, the stored RMS code decoded directly. A file being
written, a level above the chunk and the clip's edges keep the general query.
About 40 lines.

**Tests** (`test_waveform_grid`): min and max bit for bit the general
query's, the RMS within one code step, on about 410 random grid views per
channel count (1, 2, 3); the RMS against the true RMS of the samples within
one code step on every grid level from 6 to 16.

**Measured:** `device_columns()` on a finished file, one hour 48 kHz stereo,
the whole file as the clip; CPU 2 (a P-core) at a fixed 1.6 GHz (performance
governor, no turbo, minimum = maximum), Debian's flags (`-O2 -flto=auto`),
best of three runs in two rounds, old (master `cd50ae2b`) and new
alternating; one program linked against either library
(`~/Development/Traverso/builds/gw-harness`, outside the repository):

| view | shift | old ns per column | new ns per column | factor | new cycles per column |
|---|---|---|---|---|---|
| 64 frames per pixel, 3840 columns | 6 | 59.1 | 17.3 | 3.4 | 28 |
| 256 | 8 | 63.5 | 16.1 | 3.9 | 26 |
| 1000 | 10 | 72.8 | 20.8 | 3.5 | 33 |
| 4096 | 12 | 76.6 | 16.8 | 4.6 | 27 |
| 16384 | 14 | 84.3 | 18.9 | 4.5 | 30 |
| 65536 | 16 | 76.8 | 21.0 | 3.7 | 34 |
| 10 minutes on 1920 columns | 14 | 82.9 | 22.3 | 3.7 | 36 |

Per query in the last case: 159.2 us old, 42.8 us new (at 1.6 GHz). The
cycles include the mapping of pixels to grid columns, which old and new
share.

**Why the decision rule failed.** The work order fixed "keep it when at
least 5 times faster per column" from an estimate of about 450 cycles per
column for the old path (0.2 ms over about 1300 columns at an assumed
3 GHz). Measured, the old path costs 95 to 135 cycles per column, so the
rule judged the new path against a baseline that was wrong. The part
designed for the hardware, the walk itself, costs 26 to 36 cycles per stereo
column, inside the forecast of 30 to 60.

**Decision (Remon, 2026-10-29):** keep it. The goal was speed by designing
for the hardware, and the walk costs what the forecast said. `walk_grid()`
is temporary: it goes together with the QPainter view's query
(`from_pyramid()`) once the GPU view is there. From now on a benchmark's decision rule tests whether the part
designed for the hardware costs what the forecast says, not whether the
total is a factor faster than the old path.

