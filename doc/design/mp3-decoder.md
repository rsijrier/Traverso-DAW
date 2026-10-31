<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# MP3 on libmpg123: MpgAudioReader

Background and measurements: `reports/mp3-decoder-research.md` (not
versioned, may be missing). This document is complete without it.

Task T73 in `doc/status.md`. `MadAudioReader` (MAD, K3b code, 1031 lines)
goes; `MpgAudioReader` on libmpg123 comes in its place, measured against
the decoder contract in `TAudioDecoder.h` and `test_audio_decoders`, not
against what MAD does. Estimated size: 200 to 250 lines. Everything below
was measured in a throwaway prototype on Linux x86-64 with mpg123 1.33.3,
unless section 7 says otherwise.

## 1. Decisions (Remon, 2026-10-31)

1. libmpg123 >= 1.33 is a direct, required dependency, linked PRIVATE. One
   code path: `mpg123_open_handle64()`, no `#if` on the API version.
2. Seek tolerance for all MP3 test lines: 1e-6 (-120 dBFS). The test also
   runs with the generic decoder.
3. Scanning at open is a transition, not the final rule. The class must
   later allow opening without a scan, from a saved index
   (`mpg123_set_index64()`). After the scan `take_error()` is checked: a
   failed read gives `CannotOpen`, never a silently short length.
4. Layer I and II also go through `MpgAudioReader`.
5. Damaged files: `MPG123_NO_RESYNC` in the reader, plus a seek limit at
   what the scan found. Without a tag: a scan that stops more than 64 KiB
   before the end gives `CannotOpen` with the byte position. `RESYNC_LIMIT`
   stays bounded, never -1.
6. A file cut short before it is opened: the end is an error (contract
   point 4), and this goes before MAD's behaviour. No shift of the audio.
   The reader works out the audio's place from delay and padding as MAD
   does.
7. Threads: one process-wide mutex around our own `mpg123_new()`, and no
   mutex around `sf_open_virtual()`.
8. Probe step 3 with the result `SF_FORMAT_MPEG` gives no decoder.
9. A WAV with MP3 in it (`WAV|MPEG_LAYER_III` according to libsndfile) goes
   to `MpgAudioReader`.
10. The probe goes by content, never by file name: a table of fixed
    signatures, the strict MP3 probe, libsndfile, then the existing probes
    of WavPack and FAAD (section 3). A table entry holds fixed signatures
    only, no versions, subtypes or codecs; in doubt the library decides. A
    format that is not in the table keeps working through the later steps.
11. `.tscenario` by file name is a deliberate exception: it is a test
    facility, not an audio format.
12. Junk in front of the audio: limit 1 MiB.
13. The test chooses the generic decoder through an environment variable
    that `MpgAudioReader` reads itself (for instance
    `TRAVERSO_MPG123_DECODER`), documented. No extra parameter in the API.
14. `NO_PEEK_END` in the probe is provisional: the build confirms it with
    the tests, because it now rests on one run with unexplained behaviour
    (section 7).

## 2. Why

**Why not libsndfile.** libsndfile leaves no way to reach the mpg123 handle.
Without an Info tag `SF_INFO.frames` is an estimate from the file size, and
reads and seeks stop at it: a VBR file without a tag lost 79 % of its audio
without an error. A CBR file without a tag reports more frames than it has,
so a healthy file ends in an error. Traverso cannot repair either from the
outside, and the last libsndfile release is from 2023.

**What a seek really does.** mpg123 lands exactly. The comment "mpg123 lands
some hundreds of frames off" in `find_decoder()` is wrong. A seek empties the
bit reservoir, a frame that points back further than the bytes that are there
becomes a silent frame, and mpg123 decodes only 4 frames before the target by
default. `MPG123_PREFRAMES` is the setting for this; libsndfile does not set
it.

**Why 1.33.** Building from source happens on current systems (Debian
unstable, Ubuntu 26.04). Older systems such as Debian trixie (1.32.10) and
Ubuntu 24.04 (1.32.5) get an installer, with mpg123 included or statically
linked. `mpg123_open_handle64()` exists from 1.33 on, and it saves a second
code path. `MPG123_PORTABLE_API` keeps `off_t` out of our code; it needs
`mpg123_info2()` instead of `mpg123_info()`.

**Why a seek tolerance.** After a seek the samples differ from plain decoding
in the last bits (up to 3e-7 at peak 1.13). The synthesis phase `fr->bo` moves
on with every synth call and a seek does not restore it, so the same seek can
round differently depending on what the handle decoded before. A plain reset
of `bo` does not help; the phase has to follow from the first decoded frame.
The position is right and the difference is far below hearing. MPEG-1 stereo
is bit exact with AVX, but not with generic.

**Why `NO_RESYNC`.** With resync mpg123 finds its way back after damage, but
the audio after the damage then sits at another place, without an error, and
seeks in that part land wrong. Junk also decodes as loud clicks. For a DAW a
wrong place is worse than an end. With `NO_RESYNC` the audio is exact up to the
damage, then the read fails at the right place. The price is known: the rest
of the file cannot be reached, also for a few wrong bytes, where MAD played on.
MAD kept the time only for inserted junk; with damage in place it shifted too,
silently.

**Why a seek limit next to `NO_RESYNC`.** `NO_RESYNC` stops a read, but a seek
past the damage still lands, silently wrong. A seek at or past what the scan
found must fail (`SeekFailed`); a seek back reads again (contract point 3).

**Why no mutex around libsndfile.** libsndfile calls `mpg123_new()` itself in
two cases: a WAV with tag 0x55, and a file the strict probe refused that starts
with a valid frame header. No order of the probes prevents this, because
libsndfile only tells what it found after it opened the file. A mutex around
`sf_open_virtual()` would be held during IO, and a slow NAS would hold up all
opens.

The remaining risk is documented and accepted. A crash can only happen on armhf
without NEON, when libsndfile opens such a file at the same moment as another
thread calls `mpg123_new()`. On ARM mpg123 finds NEON with a process-wide
SIGILL handler and a shared static `jmpbuf`, per handle. The `aarch64` builds
run the same code, but NEON is always there, so no signal comes; only a race
on the handler is left.

**Why the reader handles delay and padding itself.** With `MPG123_GAPLESS` on,
mpg123 switches gapless off by itself when the scan finds fewer frames than
the tag promises. That shifted the audio of a cut file by 1105 frames.
`MPG123_ENC_DELAY` and `MPG123_ENC_PADDING` are read with gapless off as well.

**Where MAD differs on a cut file.** MAD counts the frames it finds and reports
no error at the end. Contract point 4 goes first: the length is the tag's, the
read fails where the audio ends, and the audio stays in place.

**Why WAV with MP3 goes to mpg123.** libsndfile reads these with the same
length defect as plain MP3 (VBR: 79 % gone). mpg123 skips the RIFF header
itself and reads them whole.

**Why probe step 4.** ADTS, and WavPack behind an ID3 tag, have no table entry
and libsndfile does not read them.

**Why a limit of 1 MiB.** A behaviour change against MAD, which refused none of
the files with junk in front that were tried. `MPG123_RESYNC_LIMIT` can only
raise mpg123's 64 KiB, never lower it. Files with a signature pay nothing.

## 3. `find_decoder()`

1. `.tscenario`, by file name (decision 11).
2. The table below. A match goes to that decoder directly.
3. `MpgAudioReader::can_decode()`, the strict probe.
4. `SFAudioReader::major_format()`: `SF_FORMAT_MPEG` gives no decoder;
   `WAV|MPEG_LAYER_III` goes to `MpgAudioReader`. The second also holds on
   the RIFF route of the table: it is known after libsndfile opened the file.
5. The existing probes of WavPack and FAAD.

| Signature | Offset | Decoder |
|---|---|---|
| `RIFF`, `RF64`, `riff` (W64), `FORM`, `fLaC`, `OggS`, `caff`, `.snd` | 0 | libsndfile |
| `wvpk` | 0 | WavPack |
| `ftyp` | 4 | FAAD |

Also fixed, not tried: `RIFX`, `dns.`. `ID3` is not in the table: a tag can
precede MP3, FLAC or WavPack.

`can_decode_audio()` must really open the file: a table entry alone proves
nothing.

### The strict probe

A handle with the settings of section 4, plus `NO_PEEK_END` (decision 14). Open
it, then walk frames with `mpg123_framebyframe_next()`, comparing layer, version
and rate with the first frame. The open reads one frame already, so n frames
walked is n + 1 frames.

The file is MP3 when one of these holds:
- 4 frames walked, all alike;
- a clean end of the stream after at least 2 frames walked;
- a clean end of the stream after at least 1 frame walked, and a LAME tag
  (`MPG123_ENC_DELAY >= 0`).

On random bytes opening alone gives false hits (about 1 in 600 to 2500 for
files of 70 KB). No false hit was seen at 2 or more frames walked. One was seen
at 1 frame walked and a clean end, so that case needs the tag. An untagged file
of 2 MPEG frames (52 ms) is refused; such a file gave a wrong length through
libsndfile before.

## 4. `MpgAudioReader`

**Handle.** `mpg123_new()` under the mutex, the mutex only there.
- Remove: `MPG123_AUTO_RESAMPLE`, `MPG123_GAPLESS`.
- Add: `MPG123_FORCE_FLOAT`, `MPG123_QUIET`, `MPG123_NO_RESYNC`,
  `MPG123_SKIP_ID3V2`. The last one skips a tag of any size instead of
  reading it in every handle (album art).
- Not `MPG123_NO_FRANKENSTEIN`: it cuts off files that are concatenated.
- `MPG123_RESYNC_LIMIT` 1 MiB, the same in the probe and the reader. With
  `NO_RESYNC` it only sets the search at the start.
- `TRAVERSO_MPG123_DECODER` chooses the decoder (test: `generic`).
- `mpg123_reader64()` on the `TByteSource`, `mpg123_open_handle64()`.
  Define `MPG123_PORTABLE_API`.

**Open.**
1. `mpg123_getformat()`.
2. Read delay, padding and `mpg123_length64()` before the scan.
3. `MPG123_INDEX_SIZE` 10000: at most 80 KB per handle. The default of 1000
   entries reads about 94 KB per seek on an hour of audio, 10000 about 14 KB,
   an index per frame about 9 KB but 1.1 MB per hour per handle. There are two
   handles per file (stream and peak build).
4. `mpg123_scan()` as a step of its own, not fixed in the constructor
   (decision 3).
5. `take_error()` on the source: a failed read gives `CannotOpen`, because
   `mpg123_scan()` takes a failed read for the end of the file and returns
   `MPG123_OK`.
6. `MPG123_PREFRAMES` 12 for MPEG-1, 90 for MPEG-2 and 2.5 (`mpg123_info2()`,
   after the open). mpg123 limits Layer I and II to 2 itself.
7. The length, as below.

**The length.** Without a tag: the scanned samples. A scan that stops more than
64 KiB before the end gives `CannotOpen` with the byte position (decision 5).

With a tag (delay and padding >= 0):
- the source of the samples is the tag's length when it says more than the
  scan found, a file cut short; otherwise the scan, so a tag that says less
  (concatenated files) does not cut anything;
- `m_frames` = samples - delay - max(padding, 529);
- skip = delay + 529, the decoder's delay being 529.

Keep the scanned samples: they are the seek limit.

**Seek.** `frame + skip`; at or past the scanned samples `seek_failed()`;
`mpg123_seek64()` must return exactly that position.

**Read.** Clamp to `m_frames`, read in a loop until the count or the end,
de-interleave as `SFAudioReader` does. A read that ends before `m_frames` is
an error through `read_done()`.

**Preframes, the known limit.** 90 covers the 85 frames a stereo stream of
24 kHz at 8 kbps can reach back (24 bytes per frame, minus 21 of header and
side info: 3 bytes). With CRC it is 1 byte per frame, so 255 frames back. A
known limit, to be documented in the code. MAD's 16 did not reach that far
either.

**A later open without a scan.** Tried: a fresh handle with
`mpg123_set_index64()` and a saved index by frame seeks exactly and reads only
what it needs. The saved data must also hold the scanned samples: without a tag
`mpg123_length64()` stays an estimate, and they are the seek limit.

**In the code itself.** The remaining risk of "Why no mutex around libsndfile",
the exception of decision 11, the environment variable of decision 13.

## 5. Files

- `src/audiofileio/decode/MpgAudioReader.h`, `.cpp`: new.
- `src/audiofileio/decode/MadAudioReader.h`, `.cpp`: removed, with the RIFF
  hack in its `can_decode()`.
- `src/audiofileio/decode/TAudioDecoder.cpp`: `find_decoder()`, and the
  comments on MAD; the claim about "hundreds of frames" is wrong.
- `src/audiofileio/decode/TAudioDecoder.h`, `src/byteio/TByteSource.h`:
  mentions of MAD.
- `src/audiofileio/decode/SFAudioReader.h`: its comment still names MP3.
- `src/audiofileio/CMakeLists.txt`: `PkgConfig::MPG123` PRIVATE, `MAD` out.
- `cmake/Dependencies.cmake`: `pkg_check_modules(MPG123 REQUIRED
  IMPORTED_TARGET libmpg123>=1.33)`, `MAD` out.
- `debian/control`: `libmpg123-dev (>= 1.33)` for `libmad0-dev`.
- `traverso.iss`: `libMAD.dll` out. Windows links statically (`MPG123_STATIC`
  in `CMakeLists.txt`, `mpg123` in `src/traverso/CMakeLists.txt`), so no DLL
  takes its place; an assumption, not built.
- `tests/audiofileio/test_audio_decoders.cpp`: `seekTolerance` 1e-6 on the four
  MP3 lines; read the comment "(mpg123 does)" again.
- `tests/audiofileio/test_audio_byte_source.cpp`: the comment "Through MAD".
- libsndfile stays for writing MP3: its encoder does not use mpg123.

T11 and the MAD points of T71 fall away when this is done.

## 6. New tests

- VBR without a tag: the whole length.
- A bit reservoir reaching further than 16 frames.
- Junk in front of the audio: below and above 1 MiB.
- An ID3v2 tag of 1 MiB, in memory, in `test_audio_byte_source`: open, length,
  a seek, and the bytes read (`io_stats()`) below the file size minus the tag.
- Short files: the rules of the strict probe, with and without a tag.
- Damage in the middle, with `best_lag`: exact up to the damage, an error at
  the damage, a seek past it refused, a seek back reads.
- Damage without a tag: the `CannotOpen` of decision 5.
- A file cut short before it is opened: the error at the end, the audio in
  place.
- A read that fails during the scan: `CannotOpen`, with and without a tag.
- A WAV with MP3 in it.
- Concatenated files: both parts.
- Everything MP3 again with the generic decoder.
- `NO_PEEK_END` in the probe (decision 14).

## 7. Not measured, to be done in the build

Not in the prototype:
- the route of a WAV with MP3 end to end (a WAV behind an ID3 tag was read
  right, and mpg123 opens the WAV itself with the right length);
- `SF_FORMAT_MPEG` giving no decoder in step 4;
- the `CannotOpen` at 64 KiB without a tag;
- the 1 MiB and `INDEX_SIZE` 10000 in the test runs (measured apart).

To settle:
- A change of format inside a file (`MPG123_NEW_FORMAT` with another rate or
  channel count). Proposal: treat it as damage, stop with an error.
- `NO_PEEK_END`: with it `mpg123_getformat()` reports OK on short junk far more
  often, for a reason not found. Walking the frames still refused those files,
  and the probe gave the same results in the tests. If the tests do not confirm
  it, it goes.

Not checked at all: other versions of mpg123, macOS, Windows, ARM, big-endian,
Layer I, MP3 files of other encoders than LAME and libsndfile, real damage,
hard disks and NAS.
