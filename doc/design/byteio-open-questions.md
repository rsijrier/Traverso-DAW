<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# byteio: open questions

Questions only, no answers or design. To be decided at T71 step 2c, with
the measurements. Each proposal is one to judge, not a decision.

## mlock

Locking memory (`mlock()`) is in dsp now (`TChannelBuffer`, with its
failure handler). Proposal to judge: the primitive moves to support, used
by dsp and byteio. No second implementation in byteio.

## Readahead and buffer management

- Who owns the blocks of the window? Proposal: byteio, per source.
- Who decides how much? Proposal: the ioscheduler, from priority and
  measured latency.
- Where does the memory come from? Proposal: locked through support,
  within one total budget.

## Qt in byteio

byteio uses `QString`, `QFile` and `qint64`. Proposal to judge: byteio as
the first Qt-free library. Points of attention:

- `int64_t` instead of `qint64`, without overloads on several integer
  types (on macOS and Windows `int64_t` is `long long`, the type of
  `qint64`; see T12).
- Unicode file names on Windows, which `QFile` handles now.
