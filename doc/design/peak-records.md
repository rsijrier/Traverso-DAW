<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Peak files as records in chunks (design 2026-10-20, format 4 2026-10-24)

The simplified peak system, decided with the user on 2026-10-20 under the
KISS principle: simple where possible, complex only where it must be. It
replaces the periods, the in-memory pyramid and the lock-free reader
protocol of `peak-diskio.md` (sections 2 to 4). The value codes, the RMS,
the peak cache and the DiskIO rounds stay as they were.

Format version 4 (2026-10-24, the user's proposal "Peak-bestandsformaat v4:
gechunkte pyramide" and its decisions) writes the levels while the audio
comes, in chunks. A file being written has its levels too, so painting a
long recording zoomed out no longer reads all of level 0 per repaint, and
the pass over level 0 after a recording or a build is gone.

## 1. File format: TRAVPEAK version 4

```
[header, 80 bytes][chunk 0][chunk 1] ... [chunk n - 1]
chunk: [level 0: 1024 records][level 1: 512] ... [level 10: 1 record]
```

- **A record per block**, the same for every level: the min and max code of
  every channel, then the RMS code of every channel (int16 log codes, 1024
  steps per octave). 6 bytes per channel, 12 for stereo.
- **A chunk** holds 2^C level-0 blocks and levels 0 to C of them, each level
  an array of records, level 0 first. C = 10: 65536 frames, 1.4 s at 48 kHz.
  Pairs never cross a chunk, so a chunk's levels are the pyramid's, cut to
  the chunk. There are no levels above C: a query goes through level C one
  block (one chunk) at a time.
- **Level counts round up** (ceil): level L + 1 has half the blocks of level
  L, rounded up. The last block of a level may cover fewer frames; it is
  weighted by its real frames.
- **Every chunk has the same size** (2047 records); the last one is filled up
  with zeros, at most 12 KB per channel. Every offset follows from the frame
  count and C: block k of level L is in chunk `k >> (C - L)`, at
  `level_offset(L) + (k & (2^(C - L) - 1))`. No table.
- **The file is only appended to**; a chunk does not change once written.
- **The header is written last.** Until then it is zero, so an unfinished
  file is Invalid and gets rebuilt (it is a cache). It holds the source's
  rate, channels, frames, size and modification time, the chunk level C (a
  reader takes the file's own, 1 to 20; this version writes 10), and a
  checksum.

## 2. Writing: one path for recordings, builds and imports

`TPeakStream`:

1. `open()`: a part file of its own, `<peak>.<pid>-<n>.part`, never one that
   exists already; 80 zero bytes for the header.
2. `append()`: every 64 frames a level-0 record, and from its decoded codes
   the levels above it: per level a block waits for its pair, a pair makes
   the block above it, up to level C.
   - Min and max are combined codes: exact.
   - The RMS comes from the mean squares of the decoded level-0 codes,
     weighted by frames, rounded once: within one code step (0.0059 dB) at
     every level. The user accepted this as invisible. A recording makes the
     same file as a build from its audio.
   - Levels 1 to C of the chunk being made are in memory (1023 records, 12 KB
     per channel) and follow the chunk's level 0 in the file when the chunk
     is full.
   - After each append the records are written, flushed, and the frames they
     cover are published in one atomic count (release). The complete chunks
     follow from that count: a chunk's last block and its levels above are
     written together. Nothing else is shared between threads.
3. `finish(audioPath)`: the partial last block; in the last chunk a block
   without its pair covers the end of the source alone and moves up as it
   is, level by level; zeros for the blocks the chunk does not have. The
   source's size and time for the header.
4. `post_process()`: the header, then the rename to the peak file's name
   (`TPeakFile::publish()`). Windows renames no file that is open, and a
   waveform maps it for a moment per repaint (a virus scanner or the indexer
   may open it too): there a refused rename is tried again in the next
   steps, for two seconds, never in a sleep inside a round. The part file
   stays meanwhile.

Who calls what:

| Writer | append | post_process |
|---|---|---|
| Recording | its writer, on the write DiskIO | a job of `TPeakBuildService` in the read DiskIO's idle time, asked for by `TAudioClip::finish_write_source()` |
| Build | `TPeakBuildTask` steps in DiskIO's idle time | the same task's next step |
| Import, merge | their own thread | at once, in `finish_export()` (until imports move to DiskIO, a separate work order) |

`post_process()` is one short step now. The recording's job and the file
waveform joining it (section 3) stay until the glue around them is made
simpler, after the merge of the IO scheduler branches.

**Write failure** (a full cache disk) never affects the recording: the
stream stops writing, the count freezes, the log says it once. `finish()`
removes the part file; there is no header, and the waveform builds the file
from the audio afterwards. (On Windows a removal while the waveform reads
fails; the part file then goes with the cache's eviction of part files older
than a day.)

## 3. Reading

- **While a file is written** the waveform reads it through
  `TPeakStream::read()`: per call it opens the file, maps what is written
  (the complete chunks, then level 0 of the chunk being written), runs the
  query on that view (`TPeakLevels`) and unmaps. Nothing stays open between
  reads: no handle or mapping keeps Windows from renaming the file.
- **A query splits its range** (`visit_range`) at the end of the complete
  chunks: before it the pyramid, after it level 0 one block at a time, at
  most 1024 blocks. That end is a multiple of 2^C, so no pair crosses it.
- **Cost:** a fresh mapping costs a page fault per page the query touches;
  zoomed out, one page per chunk in view. Measured (section 4): 10 minutes in
  view 0.63 ms per repaint, a whole hour 4.24 ms (14.25 ms with level 0
  alone). Up to about 10 minutes in view this is the trade the user chose:
  no state between reads. If wider views during a recording matter, the next
  step is a mapping kept between reads (remapped when a chunk is complete),
  with an explicit order between letting go of it and the rename on Windows.
  The format stays the same.
- **The view is the file's:** every record read has its final value, on
  every level of the complete chunks. A column's min and max are the
  finished file's; its RMS, where the column reaches into the chunk being
  written, is within a step (that part comes from level 0).
- **Between the rename and the published flag** a read can find the part
  file gone and the flag not yet set: that repaint shows no waveform, and
  the job's end repaints right after it. Accepted.
- **After the stop** the recording's file waveform asks for its peak file,
  finds the post-processing job of the same file and joins it; it maps the
  finished file when the job is done. Once renamed, a read through the
  stream reads from the file's name (the records are in the same place).
  When the job ends in the moment between the waveform's open and its
  request, the waveform builds the file again from the audio: a wasted
  build, the result is the same. When post-processing fails, the waveform
  builds once from the audio.
- **A raster view** asks for one column per device pixel; from more than 32
  frames per pixel they come from a power-of-two grid in source time, about
  one record per column (`waveform-grid.md`).
- **A finished file** is mapped (`TPeakFile`) and stays mapped. Columns take
  the middle of a column from the highest level that fits and the edges
  from the levels below; `max_amplitude()` is the bound from the levels plus
  exact reads at the edges. No maxima store.

## 4. Measured

Release, one hour of 48 kHz stereo, 3840 columns (`traverso_peak_benchmark`):

| | version 3 | version 4 |
|---|---|---|
| making the pyramid | level 0 564 ms + post-processing 175 ms | 773 ms, all levels while written |
| file size | 64.8 MB | 64.8 MB |
| while written, the whole hour in view | 14.25 ms (level 0 alone) | 4.24 ms (mapped per read) |
| while written, 10 minutes in view | 2.30 ms | 0.63 ms |
| finished file, 10 minutes in view | 0.28 ms | 0.32 ms |

Headless app (Debug), Minidisk opname, 52 builds: all built in 10.4 s
against 14.7 s on master; the other projects within the noise.

Version 3 against the periods (2026-10-20): making the pyramid 551 + 172 ms
against 732 + 18 ms; the whole hour from the finished file 0.57 ms against
0.81 ms; headless, Bendictus, build span 1.04–1.08 s against 1.6–2.1 s.

## 5. What went

- `TPeakPyramidBuilder`: the in-memory pyramid, its pages, published counts
  per level, released chunks and poisoned blocks;
- the periods and the tail, `TPeakFileWriter`, `write_periods()`;
- the snapshot of memory plus mapping, `remap()`, the reader protocol;
- the publish on the GUI thread and the posting of results to it.

The peak files (`TPeak*`, `TWaveform*`) went from 2606 to 2098 code lines.

In version 4: the pass over level 0 after writing (`PostProcess`,
`write_level()`, the levels' output buffers) and the windows of level 0 in
`TPeakLevels` (`Level`). With the chunks added, the peak files went from
1962 to 1935 code lines.

## 6. Tests

Headless, on the pure classes (`tests/peaks`):
- a round trip against brute force (mono to three channels, lengths around
  blocks, pairs and chunks, partial last blocks, the RMS within half a step
  at level 0 and a step above);
- the levels made while writing against a separate pass over level 0 (the
  rules of version 3), every record byte for byte: lengths around the chunks'
  edges, one to six channels, NaN, inf and samples beyond +24 dB
  (`test_peak_chunks`); an RMS combined without the frames' weights fails it;
- appends in any chunk size, builds in any slice size: the same file;
- a crash before the header, while the chunks are written or after the last
  one: Invalid; a stream given up or gone before its header leaves nothing;
- a write failure in the middle of a recording (the process's file size
  limit): the count freezes, what was written stays readable, the stop
  leaves no file;
- a cancel while the chunks are written and before the header;
- the hand-over: a recording's post-processing as a task of its own;
- stress, also under ThreadSanitizer: a writer appends 256-frame buffers
  while a reader queries what is written; every record read, on every level
  of the complete chunks, equals the finished file's, and so do the columns
  (min and max exactly, the RMS within a step);
- the benchmark of the live and finished queries (section 4).

Not tested headless: the app's recording path (writer, clip, service), which
needs a runtime test, and the mapping of a file that another handle still
appends to on Windows and macOS.

## 7. Building one file in parallel (Remon, 2026-10-31; after the release)

A design note, nothing of it is built. It replaces the stop-gap of
`diskio-app-wide.md` section "Decided by the user on 2026-10-23" and fills in
T19: a long compressed file is built last and alone, on one core. When this is
in, `TIOIdleTask::weight()` and the heaviest-first rotation go.

**One builder, one file.** A peak builder owns one file: the handles of its
decoders, the peak file in memory, and the moment it is written.

**Chunks are the unit.** The file is divided on the chunks of section 1: their
boundaries fall on a multiple of the block of the top level (64 frames at level
0, a factor two per level). Today C = 10, 65536 frames, about 1.5 s at 44.1 kHz;
C = 11 would be about 3 s, and a reader takes C from the file. Pairs never cross
a chunk, so every chunk builds its whole pyramid itself: there is nothing to
merge.

**Tasks.** Chunks go as tasks ("decode chunk i for builder B") to the shared
decode pool. Every decoder has its own handle; the mutex around `mpg123_new()`
makes that safe. An MP3 chunk starts with its preframes from the chunk before:
at most 90 frames of overlap (`mp3-decoder.md` section 4).

**Fairness.** A maximum number of decoders per builder, so one large file does
not hold up the small ones. Playback always goes first.

**The scan once.** An MP3 is scanned once; the index is kept next to the peak
file, so the next open does not scan (`mpg123_set_index64()`). The kept data also
holds the scanned samples: they are the seek limit, and without a tag
`mpg123_length64()` stays an estimate (`mp3-decoder.md` section 4).

**Per medium.** The scheduler chooses from what it measured of the medium.
- Fast (SSD): first the scan, then the decoders read for themselves.
- Slow (HDD, NAS): one reading thread delivers the chunks in sequence and the
  decoders work from memory. Memory stays bounded: at most as many chunks as
  there are decoders, plus one being read.
- WAV and other uncompressed material gain nothing from several decoders: the
  disk is the limit there, one reader as today.

**Waits for.**
- The kept index: T71 step 4 ("the MP3 seek index kept next to the peak file").
- The read pool per medium and the decode pool: T71 step 2c, T61.
- The windows of byteio: T71 step 2c, `byteio-open-questions.md`.

**Where this goes beyond the decisions so far, to be settled when it is built.**
- The scan decodes nothing, but it does read all of the audio's bytes, not only
  the frame headers: libmpg123 reads each frame whole (measured: all 93,538 bytes
  of an MP3 at the open). On a slow medium the scan costs a full read of the file.
- Section 1 says the file is only appended to, in order. Chunks now finish out of
  order, so the builder keeps finished chunks in memory and writes them in order.
  The memory bound above covers the chunks being read, not these.
- After a seek MP3 samples differ from plain decoding in the last bits (seek
  tolerance 1e-6), so a parallel build need not equal a sequential one to the
  bit; whether a code can differ by a step is not measured.
- Other compressed formats are not decided: FLAC and WavPack, and whether AAC
  with its seek tolerance of 0.002 and Ogg belong here at all.
- The gain is not measured.
