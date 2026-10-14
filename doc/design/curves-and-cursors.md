<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Curves and canvas cursors (analysis, 2026-10-14)

How `TCurve`, `TFadeCurve`, `TCurveNode` and their views share data with the
audio thread, compared with the other real-time classes, and how commands and
controllers drive the canvas cursor. Input for the TCurve step of the RT
migration and for branch 3 (controller types + input layer). Read-only
analysis, master b6c83933. Companion of `input-and-viewport-flow.md`.

## 1. The players

| Class | Role |
|---|---|
| `TCurve` (TContextItem) | Automation/gain curve: a TRealTimeLinkedList of nodes, spline solving (`solve()`), evaluation (`get_vector()`, `multipoint_eval()` with a lookup cache), `process()` in the audio thread, makeup gain ramping. |
| `TFadeCurve` : `TCurve` | Fade in/out of a clip. Regenerates its own nodes from shape parameters (bend, strength, mode) in `solve_node_positions()`. |
| `TCurveNode` | Light value type, not a QObject (2007 choice): `when`, `value`, spline coefficients `coeff[4]`, intrusive `next`, back pointer to its curve. Changes are signalled by the curve (`nodePositionChanged`). |
| `TCurveView`, `TFadeCurveView` | Draw a curve. Each owns a **second `TCurve`** (`m_guicurve`) in pixel units, kept in sync with the model curve by signals. |
| `TCurveNodeView` : `TViewItem`, `TCurveNode` | Draws a node and **is itself a node** of the view's mirror curve. |
| `TAudioClipView` | Paints the waveform multiplied by clip gain curve, track gain curve and fades, evaluated on the **model** curves. |

## 2. Thread ownership compared

| Data | Clips and tracks (after RT migration) | Gain, mute, solo values | Curves today |
|---|---|---|---|
| List of elements | GUI owns, audio thread reads an immutable snapshot (TRtList) | — | TRealTimeLinkedList, shared |
| Element values | written in place by the GUI (clip location) | handed over with `post_rt_task` (`TAudioProcessingNode.cpp:116`) | written in place by the GUI (`MoveCurveNode.cpp:91`, `TFadeCurve::solve_node_positions` at `TFadeCurve.cpp:246`) |
| Derived/cached data | none shared | none shared | **written by both threads**: `coeff[4]` (`TCurveNode.h:55`), `m_lookup_cache`, `m_changed`, recomputed by whichever thread evaluates first (`TCurve.cpp:470` → `solve()` at `TCurve.cpp:225`, cache at `TCurve.cpp:605`) |
| Who evaluates | — | — | audio thread (`TCurve.cpp:192`, `TFadeCurve.cpp:192`) **and** GUI waveform painting on the same objects (`TAudioClipView.cpp:320, 336, 360`) |

## 3. Findings

| # | Finding | Evidence | Kind |
|---|---|---|---|
| C1 | Curves are the only real-time classes whose cached state is written by both the GUI and the audio thread: spline coefficients and lookup cache are recomputed lazily by whichever thread calls `get_vector()` first | §2 | **data race** |
| C2 | Node values are changed in place while the audio thread interpolates them (node moves, fade shape changes) | `MoveCurveNode.cpp:91`, `TFadeCurve.cpp:246` | race (shared with clips, made worse by C1) |
| C3 | Views keep mirror curves in pixel units, and the node view inherits the model node class; no other view in Traverso is a model element. Built to avoid double calculation, but the waveform painter evaluates the model curves anyway (C1) | `TCurveView.cpp:58, 229-235`, `TFadeCurveView.cpp:53-60` | design |
| C4 | `printf()` in the audio thread path | `TCurve.cpp:172, 211` | **not RT safe** |
| C5 | `TCurveNode` not being a QObject is not the problem: performance is fine either way, and QML can show nodes through list model roles. The problem is shared mutable and cached state | — | conclusion |

## 4. Canvas cursor: how it is driven

```
command / controller ──set_canvas_cursor_type(enum)──┐
                     ──set_canvas_cursor_data(QVariant)──┤→ cpointer() → TViewPort → TSheetView → TCanvasCursor
                     ──set_canvas_cursor_text(text) (legacy)┘                                         │
TSheetView::context_changed (no hold) → type Default + letter                     TCursorRenderer → strategy per type
                                                                                   (default, hold, fader, panning,
                                                                                    zoom, automation, channel strip)
```

| # | Finding | Evidence | Kind |
|---|---|---|---|
| K1 | Untyped protocol: `QVariant` carrying QString, QByteArray, double, QChar or TTimeRef, with a magic `"[DRAG]"` prefix that the automation renderer parses | `MoveCurveNode.cpp:144`, `Fade.cpp:128`, `TAutomationCursorRenderer.cpp:24` | design |
| K2 | Cursor texts are not translated ("Zooming", "Horizontal", "Multiple Nodes", "-inf dB") | `TZoomCommand.cpp`, `MoveCurveNode.cpp` | i18n |
| K3 | Two cursor type enums (`TContextPointer::CursorType`, `TraversoDAW::CursorType` for controllers), mapped in the viewport, only Split mapped | `TViewPort.cpp:300` | design |
| K4 | The context letter is the second character of the class name: audio clip, audio track and audio plugin all show "A", curve and curve node "C" | `TSheetView.cpp:1393` | **UX**, naming as semantics |
| K5 | Legacy cursor text next to the renderers, in 16 files | `set_canvas_cursor_text` | duplication |
| K6 | About 20 command files drive the cursor directly through `cpointer()` | — | coupling |

## 5. Proposals

### Curves (TCurve step of the RT migration)
1. The GUI owns the nodes in a TRtList ordered by `when`; node edits go through a
   transaction, like clips and tracks.
2. Solving happens in the GUI thread when a transaction publishes: the audio thread
   receives an immutable snapshot of solved segments and keeps its own lookup cache.
   Nothing derived is shared.
3. GUI evaluation (curve views, waveform painting) uses the GUI side, in time units,
   converted to pixels through the timeline view model. The mirror curves and
   `TCurveNodeView : TCurveNode` go away.
4. `TCurveNode` stays a light value type (no QObject), exposed to QML through list
   model roles.
5. No `printf()` in the audio thread.
6. Testable: the solved snapshot can be dumped and compared headlessly, like
   `tests/shortcut-parity`.

### Cursors (branch 3)
7. Each controller type exposes a typed cursor state (kind, value, unit, translated
   label, valid/invalid) that its renderer draws; one cursor type enum; no strings
   with prefixes.
8. The context cursor identity comes from explicit item registration (shortcut
   system, explicit items) instead of a letter from the class name.
9. Legacy cursor text disappears as the commands are ported.

## 5b. Drawing curves from the time domain: validity check

Question (user): can views use the curve solved in the time domain and transform
it into pixels, instead of solving a mirror curve in pixel units?

1. **Shape: valid.** The constrained cubic spline (Kruger) only uses ratios of
   differences (slopes Δy/Δx, second derivatives Δy/Δx²). A uniform scale of x
   (time → pixel) or a shift transforms every term consistently, so the curve is
   identical up to rounding. Evaluate in time, then
   `pixel = (time − curve start offset) / time per pixel`.
2. **Precision: equal in both domains, and a real issue.** The coefficients describe
   one absolute polynomial per segment, `a + b·x + c·x² + d·x³`, evaluated at the
   absolute x (`TCurve::solve`, `TCurve::multipoint_eval`). Late in the timeline and on
   short segments the terms cancel catastrophically; the error grows with
   (x / segment width)³, which is unit independent. Example: a 10 ms segment of a
   track curve after one hour gives a ratio of ~360,000, cubed ~5·10¹⁶: a double can
   be off by whole units. This affects the audio thread today. **Fix:** store the
   coefficients per segment relative to the segment start (x − x_start).
3. Minor, valid in both domains: views sample every third pixel and add node
   positions explicitly; `get_vector()` uses a different sampling step for two nodes
   (`veclen − 1`) than for more (`veclen`), cosmetic; linear interpolation accumulates
   in float, small drift.

Consequence: views draw from the shared solved snapshot in time units with one
time ↔ pixel conversion (timeline view model); segment-relative coefficients make
results slightly more accurate, so curve tests compare within a tolerance.

## 6. Open questions (UX, for the user)
1. Do the smart cursors help in practice? Is the context letter useful, and should it
   become an icon or short name instead of one ambiguous letter?
   **Answer (2026-10-14):** for the user, yes; this may change when more users use
   it. Keep them, make the context identity explicit (proposal 8).
2. During a hold the system cursor is hidden and the canvas cursor is drawn: does that
   feel right with the mouse, and with the keyboard?
   **Answer (2026-10-14):** the hardware cursor is shown only during development, to
   see how the canvas cursor follows mouse input. In a distribution the canvas (or
   QML) cursor is always the only cursor over the canvases, and the hardware cursor
   is hidden, not only during holds. Today the code only hides it during a hold
   (`TContextPointer::hold_start`, `QGuiApplication::setOverrideCursor(Qt::BlankCursor)`).
   Proposal 10: hide the hardware cursor over the canvases at all times, with a
   developer setting to show it.
