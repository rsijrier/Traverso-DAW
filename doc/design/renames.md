<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Renamed names

Names that changed, with the date, for older documents, commits, other
forks and agents: a name in an older text means what it meant then.

| Date | Before | Now | Note |
|---|---|---|---|
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
