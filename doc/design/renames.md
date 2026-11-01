<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Renamed names

Names that changed, with the date, for older documents, commits, other
forks and agents: a name in an older text means what it meant then.

| Date | Before | Now | Note |
|---|---|---|---|
| 2026-11-01 | `TByteSource::read_failed()`, `m_readFailed` | gone | `read_medium()` gives `TMediumRead {bytes, reason}` |
| 2026-11-01 | `TByteSource::now_nanoseconds()`, `TRtTransportPosition::now_nanoseconds()`, `TFrameClock::now_nanoseconds()` | `TClock::now_nanoseconds()` |  |
| 2026-11-01 | `SFAudioReader::major_format()` | `format()` | gives the subtype and the reason |
| 2026-11-01 | `TAudioDecoder::open_source()` | `source()`, `open_extra_source()` | the first source comes from the probes |
| 2026-11-01 | `TAudioDecoder::is_valid()`, `open_error()`, `m_openError` | gone | a decoder that cannot open does not exist |
| 2026-11-01 | `TScenarioDecoder::is_scenario_file()` | gone | a scenario by its first line |
| 2026-11-01 | `DecoderLibrary::None` | gone | `find_decoder()` gives `std::expected` |
| 2026-11-01 | `TAudioFileError::openCost` | gone | back when it has a user, for instance at 2c |
| 2026-11-01 | `open_audio_decoder()`, `can_decode_audio()` | `TAudioDecoderFactory::open_file()`, `can_read()` | so the base class names one friend, the same as the decoders |
| 2026-11-01 | `has_audio()` | `require_audio()` | it gives a status |
| 2026-10-31 | `TByteTransfer` | `TReadTotals` |  |
| 2026-10-31 | `TByteStats` (`TByteStats.h`) | `TReadsByOrigin` (`TReadsByOrigin.h`) | by where the bytes came from |
| 2026-10-31 | `untold` | `unclassified` | the reads the platform could not tell apart |
| 2026-10-31 | `TByteSource::set_stats()` | `count_reads_into()` |  |
| 2026-10-31 | `TByteSource::transfer()` | `read_and_count()` |  |
| 2026-10-31 | `TAudioPhaseStats` | `TAudioIOCost` |  |
| 2026-10-31 | `TAudioIOStats::Phase`, `phase` | `Activity`, `activity` |  |
| 2026-10-31 | `TAudioPhaseStats::bytes` | `TAudioIOCost::reads` |  |
| 2026-10-31 | `TAudioIOStats::current()` | `current_cost()` |  |
| 2026-10-31 | `TAudioDecoder::stats()` | `io_stats()` |  |
| 2026-10-31 | `set_phase()` | `set_activity()` | |
| 2026-10-31 | `TAudioDecoder::opened()` | `adopt_open_cost()` | takes the whole cost of the open |
| 2026-10-31 | `TAudioDecoder::frame()`, `m_frame` | `next_frame()`, `m_nextFrame` |  |
| 2026-10-31 | `TAudioDecoder::reads_from()` | `next_read_is_at()` |  |
| 2026-10-31 | `TAudioDecoder::error()`, `m_error` | `in_error_state()`, `m_inErrorState` |  |
| 2026-10-31 | `TBufferedAudioStreamReader::m_error` | `m_errorCode` | an error code, not a state |
| 2026-10-31 | `no_decoder()` | `why_no_decoder()` | `TAudioDecoderFactory.cpp` |
| 2026-10-31 | `TAudioReader::m_frame` | `m_nextOutputFrame` |  |
| 2026-10-31 | `TAudioReader::reset()` | `reset_resampler()` |  |
| 2026-10-31 | `src/audiofileio/TByteSource.h`, `TByteSource.cpp`, `TByteStats.h` (in `traversoaudiofileio`) | `src/byteio/` (library `traversobyteio`) | moved, unchanged: all disk access goes through it (`src/byteio/README.md`); audiofileio links it |
| 2026-10-30 | `TAudioByteSource` (`TAudioByteSource.h/.cpp`) | `TByteSource` (`TByteSource.h/.cpp`) | bytes, positions, time and the medium; nothing of audio (T71) |
| 2026-10-30 | `TAudioTransfer` | `TByteTransfer` (`TByteStats.h`) | |
| 2026-10-30 | `cached`, `medium`, `untold` in `TAudioPhaseStats` | `TByteStats` (`TAudioPhaseStats::bytes`) | what the source measures; the source counts into a `TByteStats*`, the decoder points it at the phase (`TAudioDecoder::set_phase()`) |
| 2026-10-30 | `TAudioSourceOpener`, `TAudioFileSource` | `TByteSourceOpener`, `TFileByteSource` | |
| 2026-10-28 | `Mixer` (`src/dsp/Mixer.h`) | `TBufferOperations` (peak, gain, mix on a buffer's samples) and `TFaderLaw` (the fader position mapping and its limits) | split (T52); not "kernels": in DSP a kernel is a filter kernel, and not all four mix (`update_peak` measures, `apply_gain` mixes nothing); not "sample": in a DAW a sample is also a sound snippet (as with `TSampleBuffer`) |
| 2026-10-28 | `Mixer::compute_peak`, `apply_gain_to_buffer`, `mix_buffers_with_gain`, `mix_buffers_no_gain` | `TBufferOperations::update_peak`, `apply_gain`, `mix_with_gain`, `mix` | |
| 2026-10-28 | `default_compute_peak` etc. | `portable_update_peak`, `portable_apply_gain`, `portable_mix_with_gain`, `portable_mix` | `x86_sse_*` and `accel_*` kept |
| 2026-10-28 | `dB_to_scale_factor()`, `Mixer::coefficient_to_dB()` | `TGain::from_dB()`, `TGain::to_dB()` (`src/dsp/TGain.h`) | audio domain, not fader |
| 2026-10-28 | `Mixer::db_to_fader_position()`, `fader_position_to_gain()`, `ZeroDbPosition` | `TFaderLaw::dB_to_position()`, `position_to_gain()`, `ZeroDBPosition` | the B of dB always capital; `TFaderLaw` is operation, not audio: it goes to the controllers with branch 3 |
| 2026-10-28 | `Mixer::min_fader_dB()`, `max_fader_dB()`, `min_fader_gain()`, `max_fader_gain()`, `clamp_dB()`, `clamp_gain()` | `TGain::MinDB`, `MaxDB`, `MinGain`, `MaxGain`, `clamp_dB()`, `clamp()` | the engine's gain range: audio domain, with the conversions in one place |
| 2026-10-28 | `FPU` (`src/dsp/fpu.h`, `fpu.cc`) | `TCpuFeatures` (`TCpuFeatures.h`, `.cpp`) | |
| 2026-10-28 | test `mixer` (`tests/dsp/test_mixer.cpp`) | `fader_law` (`test_fader_law.cpp`) | |
| 2026-10-28 | `TAudioBuffer` (one channel's samples, `src/dsp`) | `TChannelBuffer` | the name `TAudioBuffer` changed meaning |
| 2026-10-28 | `TRealTimeAudioBuffer` | `TRealTimeChannelBuffer` | |
| 2026-10-28 | `TFileIOBuffer` (`src/audiofileio`) | `TAudioBuffer` (`src/dsp`) | a list of `TChannelBuffer`s, one per channel |
| 2026-10-27 | task numbers R*n* (the rank, renumbered on insertion) | fixed task IDs T*n* | same number on that day; `doc/status.md` |
