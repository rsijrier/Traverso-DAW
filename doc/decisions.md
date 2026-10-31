<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Decision log

Implementation choices an agent made itself, inside Remon's decisions
(CLAUDE.md, "Working with Remon"). One line each: date | branch | the
choice | the reason | the files. Fable reviews check a branch's entries
against the recorded design rules; Remon reads them before a merge.
**Accepted border case** marks a choice on the edge of what always goes
to Remon, accepted by him with the reason given.

Entries for `audio-byte-source` up to 2026-10-31 were written afterwards
(2026-10-31), from the commits; the branch was built by two sessions.

| Date | Branch | Choice | Reason | Files |
|---|---|---|---|---|
| 2026-10-30 | audio-byte-source | Small reads go through a block of 16 KB (`BlockBytes`) in the byte source | the MP4 and ADTS parsers read a few bytes at a time, tens of thousands of times when a file opens | `TByteSource.h/.cpp` |
| 2026-10-30 | audio-byte-source | On Linux a read first tries `preadv2(RWF_NOWAIT)` (page cache), then `pread()` (medium); where that cannot be told, the bytes count as untold | to measure the page cache apart from the medium without a second read | `TByteSource.cpp` |
| 2026-10-30 | audio-byte-source | The file is opened as `QFile` unbuffered; Linux reads its handle with `pread()`, macOS and Windows use `QFile::seek()` + `read()` | the reads below are the medium's own; one portable path besides the Linux one | `TByteSource.cpp` |
| 2026-10-30 | audio-byte-source | The cache-against-medium test reads a file in the build directory, and reports where the file system cannot tell them apart | `/tmp` can be tmpfs, which has no medium | `tests/audiofileio/test_audio_byte_source.cpp` |
| 2026-10-30 | audio-byte-source | `_FILE_OFFSET_BITS=64` for the whole project, with a `static_assert` on `off_t` in the byte source (step 2a) | `pread()` takes an `off_t`, 32 bits on 32-bit ARM: past 2 GiB other bytes would be read | `CMakeLists.txt`, `TByteSource.cpp` |
| 2026-10-30 | audio-byte-source | The seek generation is per stream, raised on the scheduler thread where a seek is set and in the stream's own resyncs; a step carries the one it was made with (step 2b) | a stream also seeks on its own to get back in sync; a refill of an older generation stops before its next slot | `TIOStream.h`, `TIOScheduler.cpp`, `TBufferedAudioStream.cpp` |
| 2026-10-30 | audio-byte-source | A seek always resets the decoder and the resampler (`forget_position()`); `reposition()` no longer skips the seek on `reads_from()` | a seek is a full reset (step 2b) | `TAudioReader.h/.cpp`, `TBufferedAudioStreamReader.cpp` |
| 2026-10-30 | audio-byte-source | **Accepted border case.** `decoder_stats()`: one virtual function in the existing hierarchy `TBufferedAudioStream` (reader/writer), with one real user, the seek log line | no new abstraction. Rejected: a pointer (lifetime risk), moving the seek branch to the reader (rebuilds old code), dropping the line (the instrumentation is needed for step 2c) | `TBufferedAudioStream.h`, `TBufferedAudioStreamReader.h` |
| 2026-10-30 | audio-byte-source | **Accepted border case.** The seek log line in `TBufferedAudioStream::step()` on the IO thread | it only reads counters; TLog is per thread and lock-free | `TBufferedAudioStream.cpp` |
| 2026-10-30 | audio-byte-source | The seek line puts its numbers first and the file name last; one line per stream that decoded, none for a stream out of its window | a log line is cut after 256 characters; lines of zeros say nothing | `TBufferedAudioStream.cpp` |
| 2026-10-30 | audio-byte-source | The seek line shows the decoder's seek reads apart from the refill reads, per kind (cache, medium, untold) | whether a slow seek's bytes were for the seek or the refill (Remon's review) | `TBufferedAudioStream.cpp` |
| 2026-10-30 | audio-byte-source | `std::generic_category().message(errno)` instead of `std::strerror()` | `strerror()` may share one buffer between threads (Remon's review) | `TByteSource.cpp` |
| 2026-10-30 | audio-byte-source | `K3bMad::findNextHeader()` and `decodeNextFrame()` loop instead of recursing | 256 MiB of zeros went more than 12,000 calls deep (measured); the stack would overflow on large non-audio files (Remon's review) | `MadAudioReader.cpp` |
| 2026-10-30 | audio-byte-source | Every read counts with its time, also one that gave nothing; `TByteTransfer::failed` counts the reads that met an error | the wait of a failed read is the interesting part on a stalling medium (Remon's review) | `TByteSource.cpp`, `TByteStats.h` |
| 2026-10-30 | audio-byte-source | A failed read is detected per read with a flag (`m_readFailed`) that `read_failed()` sets and `transfer()` clears | the error stays until `take_error()`, which a decoder calls only at the end of its call, after several reads (Fable's review) | `TByteSource.h/.cpp` |
| 2026-10-30 | audio-byte-source | The decoder repoints all its sources in one function, `TAudioDecoder::set_phase()` (agreed with Remon) | WavPack has two sources (.wv, .wvc); a forgotten one would count into the wrong phase silently | `TAudioDecoder.h/.cpp` |
| 2026-10-30 | audio-byte-source | The medium id is the first source's | the audio file's medium, not the .wvc's (Fable's review) | `TAudioDecoder.cpp` |
| 2026-10-30 | audio-byte-source | `K3bMad::seekFirstHeader()` stops on an input error; `inputSeek()` clears the error | a failed first audio read made the search loop forever; a new position is a new read (Fable's review) | `MadAudioReader.cpp` |
| 2026-10-30 | audio-byte-source | `MadAudioReader` reports "the file does not open" when its source does not open | as the other decoders (Fable's review) | `MadAudioReader.cpp` |
| 2026-10-30 | audio-byte-source | The tests build what they need themselves: a tagged MP3 (20 KiB ID3v2), sparse files of zeros, random files, sources in memory that fail at a chosen byte | reproducible cases for the hang, the recursion and failed reads, without files in the repository | `tests/audiofileio/test_audio_byte_source.cpp` |
| 2026-10-31 | audio-byte-source | After the rebase onto device-ownership the seek and the refill take the slot size from `m_bufferSlotFrames` | one source of the slot size; device-ownership's solution was the better one (Fable) | `TBufferedAudioStreamReader.cpp` |
| 2026-10-31 | audio-byte-source | Every target that sees byteio's headers links `traversobyteio` PRIVATE itself: audiofileio, waveform, core (a separate PRIVATE line next to its PUBLIC block), commands and sheetcanvas (instead of byteio's include directory), the app and the tests | the CMake linking rule (CLAUDE.md, Remon 2026-10-31); the build without any forwarded include path shows no other target reaches byteio | `src/*/CMakeLists.txt`, `tests/*/CMakeLists.txt` |
| 2026-10-31 | audio-byte-source | **Accepted border case.** byteio uses the `Q_OS_` macros of `TPlatform.h` (in support) through the force-include of `traverso_compiler_settings`, without linking support | `TPlatform.h` belongs to the compiler settings of every target, not to support as a library | `src/byteio/TByteSource.cpp`, `CMakeLists.txt` |
| 2026-10-31 | master | **Remon's decision, MP3.** libmpg123 >= 1.33 as the lower bound, one code path: `mpg123_open_handle64()`, no `#if` on the API version | building from source happens on current systems (Debian unstable, Ubuntu 26.04); older systems such as Debian trixie and Ubuntu 24.04 get an installer, with mpg123 included or statically linked | `doc/design/mp3-decoder.md` |
| 2026-10-31 | master | **Remon's decision, MP3.** The mutex only around our own `mpg123_new()`, none around `sf_open_virtual()`; probe step 3 with the result `SF_FORMAT_MPEG` gives no decoder | a mutex around the open is held during IO, and a slow NAS would hold up all opens. Remaining risk, documented, to report upstream: a crash only on armhf without NEON, when libsndfile opens a WAV with MP3 in it or an MP3 with junk in front at the same moment | `doc/design/mp3-decoder.md` |
| 2026-10-31 | master | **Remon's decision, MP3.** A WAV with MP3 in it (`WAV|MPEG_LAYER_III` according to libsndfile) goes to `MpgAudioReader` | libsndfile reads those with the wrong length (up to 79 % gone), mpg123 reads them whole | `doc/design/mp3-decoder.md` |
| 2026-10-31 | master | **Remon's decision, MP3.** Damaged files: `MPG123_NO_RESYNC` in the reader, plus a seek limit at what the scan found; without a tag, a scan that stops more than 64 KiB before the end is `CannotOpen` with the byte position | resync shifts the audio after the damage and makes seeks land wrong; this way nothing is silently wrong. The price: the rest of the file cannot be reached | `doc/design/mp3-decoder.md` |
| 2026-10-31 | master | **Remon's decision, MP3.** Probe step 4: after libsndfile the existing probes of WavPack and FAAD | ADTS, and WavPack behind an ID3 tag, have no table entry and libsndfile does not read them | `doc/design/mp3-decoder.md` |
| 2026-10-31 | master | **Remon's decision, MP3.** `.tscenario` by file name: a deliberate exception to "by content, never by file name" | it is a test facility, not an audio format | `doc/design/mp3-decoder.md` |
| 2026-10-31 | master | **Remon's decision, MP3.** A file cut short before it is opened: the end is an error (contract point 4), this goes before MAD's behaviour | MAD counts the frames it finds and reports no error; the contract is the measure, and the audio stays in place | `doc/design/mp3-decoder.md` |
| 2026-10-31 | master | **Remon's decision, MP3.** Junk in front of the audio: limit 1 MiB | a behaviour change against MAD, chosen explicitly; files with a signature pay nothing | `doc/design/mp3-decoder.md` |
| 2026-10-31 | master | **Remon's decision, MP3.** The test chooses the generic decoder through an environment variable that `MpgAudioReader` reads itself (for instance `TRAVERSO_MPG123_DECODER`), documented; no extra parameter in the API | libmpg123 has no such variable; the API stays as it is | `doc/design/mp3-decoder.md` |
| 2026-10-31 | master | **Remon's decision, MP3.** `NO_PEEK_END` in the probe is provisional: the build confirms it with the tests | it rests on one run with unexplained behaviour | `doc/design/mp3-decoder.md` |
