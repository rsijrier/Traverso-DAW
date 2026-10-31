<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# byteio

All disk access of Traverso goes through this library, so every read and
write is known, timed and counted per medium.

- Users now: audio file decoding (`audiofileio`).
- Intended users later: peak files, the write direction (recording,
  bounce) and video. They build on this library; there is no second byte
  layer.
- It knows bytes, positions and the medium only: no frames, no audio
  types, no decoder phases.
