<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# audiofileio: calls that can fail

Draft, 2026-10-27 (brainstorm with Remon; reviewed twice by Fable and by
Remon). Goal: a call into audiofileio that can fail says so in its type, and
says why, so core can act on the reason and tell the user. Scope: the public
API of `traversoaudiofileio` (`TAudioReader`, `TAudioWriter`), its decoders
and encoders, and one addition to `TAudioResampler` in `src/dsp` (E2).

## Today: five ways a failure leaves the library

| # | Pattern | Where | Problem |
|---|---|---|---|
| 1 | `unique_ptr` or nullptr | `TAudioReader::open()`, `TAudioWriter::open()` | the reason is logged inside the factory (`TAudioDecoder.cpp`); the caller knows only *that* it failed: core maps it to `COULD_NOT_OPEN_FILE` (`TBufferedAudioStreamReader.cpp:207`) |
| 2 | `bool` | `TAudioWriter::close()`, encoder `open()`/`close()`, decoder `seek()` | no reason; easily dropped (`~TAudioWriter()` discards `close()`, `TAudioWriter.cpp:56`; task T6, finding #18) |
| 3 | count + hidden state | `TAudioReader::read()`, `read_file_frames()` | a short count is the end or an error; which one is `TAudioDecoder::error()`, not visible on the reader |
| 4 | count with a special value | `TPeakBuildSource::read()` (negative = failed), `TAudioWriter::write()` (fewer = failed) | the meaning lives in comments; `TAudioFileBuildSource::read()` never returns the negative count its contract promises (`TPeakBuildTask.cpp:26-35`) |
| 5 | out parameter | `TScenarioDecoder::can_decode(path, QString* why)`; also the waveform library's convention: `TPeakStream::open(QString* error)`, `finish(path, QString* error)`, sticky `write_failed()` + `error()` (`TPeakStream.h:55-70`) | the type does not say that it can fail; status and text are two separate things |

Not reported at all: a resampler backend that fails to create logs and
keeps a null handle; `process()` then gives 0 and the stream plays zeros
(`src/dsp/TAudioResampler.cpp:40-44,129-133,154-156`);
`set_output_rate_and_resampler()` returns `void` (`TAudioReader.h:93`).

There are 31 `TLOG_` lines in the library: 30 failures, one not
(`WPAudioReader.cpp:79`, the WavPack version). FAAD logs nothing of its own;
its open failures reach the user as "the X decoder cannot read it (0 channels,
0 Hz, 0 frames)". `MpgAudioReader` logs nothing either and says why in
`m_openError`.

## Decision (Remon, 2026-10-27)

`std::expected` with one error type for calls that either succeed or fail;
per block a result that holds the count and the error of that call. The
waveform library's style (pattern 5) is not taken over: it follows this one
later (task T23).

## Proposal

**E1. One error type, in its own public header** (`TAudioFileError.h`: the
decoders and encoders need it too, and `decode/`, `encode/` are private
include directories):

```cpp
struct TAudioFileError
{
    enum class Code { NotFound, CannotOpen, ReadFailed, SeekFailed, WriteFailed };
    Code code;
    QString detail;   // the library's words: a technical addition, English
};
```

The codes are the distinctions the libraries really give and core acts on
(work order O1, parked); more only when O1 needs them (e.g. disk full via
`errno`). An unreadable file (permission) is `CannotOpen` with the detail.

`NotFound` is classified after a failed open, not checked before it (a check
before is a race, the file can go in between, and costs a system call on
every open). The one place is the `DecoderLibrary::None` branch of
`open_audio_decoder()` (`TAudioDecoder.cpp:136-142`): every probe opens the
file itself (`SFAudioReader.cpp:76-79`, `WPAudioReader.cpp:66`,
`MpgAudioReader::can_decode()`, `FaadAudioReader.cpp:327`), so a missing file,
an unreadable file and a file of an unknown format all end there. That
branch asks, in this order: does the file exist (else `NotFound`); can it be
opened for reading (else `CannotOpen`, with the system's reason from
`QFile::errorString()`); only then "no decoder reads this format"
(`CannotOpen`). A file that goes between the
probe and the decoder's open lands in the `is_valid()` branch (`:145-149`)
as `CannotOpen`. The writer never gives `NotFound`.

Core's own check before the open (`QFile::exists`,
`TBufferedAudioStreamReader.cpp:201-203`) goes: it is the race this avoids.
Its outcome stays: `NotFound` maps to `FILE_DOES_NOT_EXIST`, every other
code to `COULD_NOT_OPEN_FILE`, so whatever the GUI does with a missing file
(offering to relink it) carries over.

The user's message is built by core from the code, with `tr()`; the detail
is shown as the technical addition (libsndfile, WavPack and the others speak
English and cannot be translated). The detail is copied from the library's
`const char*` at once; `sf_strerror(nullptr)` is a global last error, which
copying does not make safe across worker threads (task T26). Where a library
gives no text, ours: FAAD, `MpgAudioReader` next to libmpg123's words, and a failed `fopen` in the WavPack and
FAAC writers (`strerror(errno)`, `WPAudioWriter.cpp:43-45`,
`FaacAudioWriter.cpp:91-93`), as after a failed `fwrite`.

**How the text gets out, below the public API.** The decoder and encoder
interfaces carry the error too, so nothing has to be logged to keep it:

- decoder `seek()` returns `std::expected<void, TAudioFileError>`; `read()`
  returns the result of E4; `seek_failed()` and `read_done()` take the error;
- a decoder that cannot open keeps the reason in a protected `m_openError`,
  which the factory copies when `is_valid()` is false;
- encoder `open()` and `close()` return `std::expected<void, TAudioFileError>`,
  `write()` the result of E4.

**E2. `std::expected` where a call either succeeds or fails:**

```cpp
[[nodiscard]] static std::expected<std::unique_ptr<TAudioReader>, TAudioFileError> open(const QString& fileName);
[[nodiscard]] static std::expected<std::unique_ptr<TAudioWriter>, TAudioFileError> open(const TAudioEncodeSettings&, const QString&, uint);
[[nodiscard]] std::expected<void, TAudioFileError> close();
[[nodiscard]] std::expected<void, TAudioFileError> set_output_rate_and_resampler(uint rate, ...);
```

Read as `if (!result) { ... result.error() ... }`; the monadic functions
(`and_then`, `transform`) are not used (KISS). `can_read()` stays `bool`: a
probe, the reason comes from `open()`.

The resampler is checked where it is made. `TAudioResampler` gets
`bool valid() const` (`src/dsp`; today the null backend is private,
`TAudioResampler.cpp:40-44,129-133`). The reader's
`set_output_rate_and_resampler()` reports a backend that did not start. The
writer's `open()` makes its resampler before the encoder opens the file:
today the encoder creates the file first (`TAudioWriter.cpp:27`) and the
resampler follows in the constructor (`:39-42`), so a failed resampler would
leave an empty file behind.

`inputSampleRate == 0` in `TAudioWriter::open()` stays an error
(`CannotOpen`): the stream writer passes the device's rate
(`TBufferedAudioStreamWriter.cpp:60`), which is 0 until a device is set up
(`TAudioDevice.cpp:169`, set at `:363`); converts and merges run without a
device. It becomes a precondition once task T6 guarantees the rate.

**E3. `[[nodiscard]]` on every call that can fail**, also those that keep
their return type (`std::expected` is not `[[nodiscard]]` by itself). No
reason text: the return type says why.

**E4. Per block: the count and the error of that call, together.** A read
can succeed and fail at once: frames 0..599 valid, 600..1023 zeros after a
damaged part, and an error; `std::expected<nframes_t, E>` holds only one of
the two. One result type holds both, for `read()`, `read_file_frames()`,
`write()` and the decoders' and encoders' block calls:

```cpp
struct [[nodiscard]] TAudioFileResult
{
    nframes_t frames;
    std::optional<TAudioFileError> error;   // set when this call failed
};

auto [frames, error] = reader->read(buffer, location, count);
```

The type says that the call can fail, and a caller that ignores the error
does so visibly (`reader->read(...).frames`). Reading the error is the
caller's responsibility: the language cannot force a field to be read, and
GCC and Clang do not warn when only `error` of a structured binding stays
unused; a binding named `error` that is never read stands out in a review.
The doc comment of `read()` and `write()` says it: a caller that only needs
the count writes `.frames`.

The error comes only from the seek or read this call made (the decoder's
return values above), never from the decoder's error state, which stays
until a seek lands and which the next read clears by seeking
(`TAudioReader.cpp:178-179`). The calls that do not touch the decoder are
not errors and must not show an earlier one: a read at or past the end
(`TAudioReader.cpp:171-175`, `TAudioDecoder.cpp:31-33`) and a position the
file cannot be sought to for rounding (`TAudioReader.cpp:146-148`): "nothing
to read there". After a write error the writer is finished, and its count is
not "what is in the file" (with a resampler a failed write gives 0,
`TAudioWriter.cpp:108`).

The resampled path reads ahead of its output (the resampler's filter), so
a read can give all the frames asked for and the error it met in the frames
beyond them; the frames before an error are always given (the resampler is
flushed there, as at the end of the file), also when the error comes while
the frames before a seek target are dropped.

`TAudioFileBuildSource::read()` keeps returning the count in this task:
returning -1 on an error would fail the whole peak build, where today a
waveform up to the damaged part remains. That behaviour change belongs to
O1 and task T10 (Remon, 2026-10-27). It writes `.frames` and drops the error
on purpose, with a one-line comment there pointing to task T10, so it does
not read as an oversight.

Considered for E4: the count plus `last_error()` on the reader (the type
does not say the call can fail, the same objection as pattern 5;
`[[nodiscard]]` forces the count to be used, not the error to be checked);
an out parameter `TAudioFileError*` (the waveform style); `expected` with
the error one call later, as POSIX `read()` does (the refill reads several
slots in one call: the error would come seconds late); one result type for
reads and another for writes (the same shape twice).

**E5. Who logs, and the destructors.** The library does not log what it
returns: the caller logs or tells the user, once. `close()` is required; the
writer's destructor asserts that it was closed, and in a release build,
where the assert is gone, it still closes and logs: a forgotten `close()`
must not silently lose the end of a recording. That fallback runs where the
writer is destroyed: for a recording the GUI thread (`TAudioClip.cpp:703`,
`:838-840`), so file IO and a resampler flush there, only in that case.
A second `close()` is a no-op success (today it returns `false`,
`TAudioWriter.cpp:61-63` and every encoder). The readers' destructors
(`~SFAudioReader()` logs a failed `sf_close`) keep their log line: nobody
else can hear it.

Not every owner closes today: `TAudioFileMerger.cpp:121-124` calls
`finish_file()` only when the merge is stopped, so a finished merge closes
through the destructor, which would hit the assert in a debug build. It
gets an explicit close in this task (it is also part of task T6, finding
#18).

**E6. Threads.** A `TAudioFileError` is handed on by value (QString is
implicitly shared). It reaches the GUI through the stream's status path
(task T10), not through a getter the GUI reads while an IO worker writes.
It never comes near the audio thread: making one allocates (the QString).
The audio thread's functions are `T_REALTIME_SAFE`, so Clang reports any
that would.

## Callers to change

- Open and close: `TBufferedAudioStreamReader.cpp:77,201-203,207`,
  `TBufferedAudioStreamWriter.cpp:66,79`, `TAudioFileMerger.cpp:121-124`,
  `TPeakBuildTask.cpp:46`, `tests/ioscheduler/measure_idle_builds.cpp:90`.
- Per block: `TBufferedAudioStreamReader.cpp:381-384,465-469`;
  `file_read()`/`file_read_frames()` (`TBufferedAudioStreamReader.h:67-68`,
  `.cpp:245-256`, `const`, returning `int`) return the result, so the stream
  can keep the error for task T10; `TBufferedAudioStreamWriter.cpp:155-161`;
  `TPeakBuildTask.cpp:26-35` (`.frames` with a comment pointing to task T10,
  see E4).
- The rate: `src/ioscheduler/TIOScheduler.cpp:265,337,568` through the
  streams.
- Tests in `tests/audiofileio`, `tests/resampling`, `tests/peaks` (E3 warns
  where a test drops a result). `TClipsViewPort.cpp:110` uses `can_read()`
  only.

## Toolchain

`cxx_std_23` (`CMakeLists.txt:83`). Checked: GCC 15.2 and Clang 21.1 with
libstdc++ 15 (`expected` with `unique_ptr` and `void`, moving out, warnings
on a dropped result); Clang 21 reports `__cpp_concepts` 202002, which
libstdc++ requires for `<expected>`. Not verified: from which Clang version
on that holds (the only Clang here is 21; Fable read it as 17), libc++ 16+
(Xcode 15), MSVC 19.33+ (`/std:c++latest`, which `cxx_std_23` selects).
GCC 12+ is libstdc++'s own requirement.

## Considered, not chosen

- **The waveform style** (`Status` + `QString*` + sticky `error()`): the
  type does not say a call can fail. The waveform library follows this
  design later (task T23).
- **`std::error_code` + `error_category`**: boilerplate, and `message()` is
  one fixed text per code.
- **Exceptions:** a damaged file is normal input; Qt and Traverso do not use
  them.
- **`std::optional`** for failures: no reason.
- **`std::span` in the buffer API:** decided against on 2026-10-24.
- **Contracts (`pre`/`post`, C++26):** later home of preconditions such as
  `Q_ASSERT(frame < m_frames)` in `seek()` and `rate == 0` in
  `set_output_rate_and_resampler()` (the caller asserts it already,
  `TBufferedAudioStreamReader.cpp:226`).
- **`std::source_location`** in the error: for a log line, not for the user;
  later if wanted.

## Open

- **Q1** The list of codes, with O1 (disk full, permission).
- **Q2** Order: this is task T9. Before task T10 (read errors hidden in
  core), which needs E4's error to reach the stream; it includes the
  merger's close (E5), also listed under task T6.
- **Q3 (decided 2026-10-27):** M4A without FAAC (`create_audio_encoder()`
  returns nullptr, `TAudioEncoder.cpp:19-25`): checked before the export
  starts, so the user is not stopped halfway (the export dialog offers M4A
  only with `M4A_ENCODE_SUPPORT`, `ExportFormatOptionsWidget.cpp:70`, and
  `TExportSpecification::is_valid()` refuses it otherwise), and still
  `CannotOpen` with a detail from `open()` as a safety net.

## Built (2026-10-27, branch `audiofileio-errors`)

- `TAudioFileError.h`: the error, `TAudioFileResult`, and
  `TAudioFileStatus = std::expected<void, TAudioFileError>`.
- Decoders: `seek()` returns `TAudioFileStatus`, `read()` a result;
  `seek_failed()` and `read_done()` take the library's words; a decoder that
  cannot open keeps the reason in `m_openError`. `MpgAudioReader::scan()`
  checks its `seek(0)`; FAAD's dead `if (!d)` and its sample-size narrowing are gone, and a
  failed start keeps its reason. `TScenarioDecoder::check()` returns
  `std::expected<void, QString>` (pattern 5 gone).
- Encoders: `open()`/`close()` return `TAudioFileStatus`, `write()` a result;
  their logs are gone. `WPAudioWriter::write_block()` no longer nulls the
  context on a failed block (`write()` dereferenced it afterwards), it keeps
  the reason and `close()` releases context and file: task T18 done here.
- The library logs only what nobody else can hear: a reader's failed close,
  the writer's release-build close in its destructor, the WavPack version.
- Core: NotFound → `FILE_DOES_NOT_EXIST` (the `QFile::exists` before the open
  is gone), the reason logged once by the caller; `TAudioFileMerger` always
  finishes its file (its peaks too, which a finished merge skipped before);
  `TExportSpecification::is_valid()` refuses M4A in a build without FAAC.
- Tests: `test_audio_file_errors` (NotFound, CannotOpen for an unknown
  format, a broken scenario and no permission; the writer without a rate or
  a directory, a second close, a write after close); `test_reader_faults`
  checks the codes (ReadFailed, SeekFailed, none at or past the end).
- Review fixes (Fable, 2026-10-27): a stream writer destroyed before its
  file was finished (a project closed between the end of a recording and
  its last disk step) closes its writer in its destructor; the WavPack and
  FAAC writers check `fclose()` (stdio's last bytes: a full disk shows
  there); a reader that refuses a rate leaves the stream at its old rate;
  `test_audio_file_errors` writes to `/dev/full` (WavPack: WriteFailed "No
  space left on device"; libsndfile fails at open, writing its header).
