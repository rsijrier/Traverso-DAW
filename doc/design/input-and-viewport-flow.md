<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Input and viewport flow (analysis, 2026-10-14)

Step 0 of branch 3 (controller types + input layer): how input flows through
`TViewPort`, `TContextPointer`, `TSheetView` and `TInputEventDispatcher` today,
where the view state (zoom, scroll, pixel ↔ time) lives, what goes wrong, and a
proposed split into clear roles. Read-only analysis, master 96e13ce6.

## 1. The players today

| Class | Library | Responsibilities today |
|---|---|---|
| `TViewPort` (QGraphicsView) | sheetcanvas | Receives Qt input events. Forwards keys and mouse buttons to the dispatcher. Mouse moves: updates `cpointer()`, filters fake moves, detects the items under the cursor (soft selection). Wheel: scrolls the sheet itself. Trackpad zoom. Mouse grab with a 30 s guard. Forwards canvas cursor calls to `TSheetView`. Scrolls its own (hidden) scrollbars for the controller shuttle. |
| `TClipsViewPort`, `TTrackPanelViewPort`, `TTimeLineRulerViewPort` | sheetcanvas | The three canvases of a sheet. Clips: drag and drop import. |
| `TViewPortInterface` | core | Abstract view of a viewport for core: map to scene/global, canvas cursor (type, text, pos, data, location), item detection, mouse grab, scrolling, size, **a copy of the time scale factor**. |
| `TContextPointer` (`cpointer()`) | core | Singleton. The current viewport. Mouse positions (viewport, global, at first input event, at hold start). Active context items (mouse or keyboard), first-input-event snapshot, permanent items (transport…), context menu items. Keyboard-only mode and "mouse takes over after N pixels". Hold start/finish: grab mouse, blank cursor. Forwards canvas cursor calls to the viewport; restores the global cursor position for some holds. **Calls `ied().jog()` from `update_mouse_positions()`.** Pixel → time for `on_first_input_event_timeref_location()`. |
| `TSheetView` (TViewItem) | sheetcanvas | The sheet's canvas controller: creates and lays out track views, track heights, **track order** (writes sort indexes), the **time scale factor** (public field `timeref_scalefactor`), scrollbars and their sync with `TSession`, follow play head, **the canvas cursor** (position, animation, type, data, overlay letter), **keyboard navigation** (`browse_to_*`, context levels), centering, snap range. ~1400 lines. |
| `TInputEventDispatcher` (`ied()`) | core | Key/button press and release, modifiers, numerical input, shortcut resolution and dispatch, hold lifecycle (legacy commands and controllers), jog, shuttle (two implementations), jog bypass, context menu dispatch. |
| `TSession` | core | Model, but also view state: `hzoom`, scroll position (`sbx`, `sby`, saved in the project), track heights, work cursor location, follow state. Child sessions mirror the parent's zoom and horizontal scroll. |

## 2. Where the view state lives (layer 3)

| State | Copies | Who writes | Who reads |
|---|---|---|---|
| Horizontal zoom | `TSession::m_hzoom` (saved) | `TSheetView::hzoom()`, zoom command, trackpad gesture | `TSheetView::scale_factor_changed()` |
| **Time per pixel** (`timeref_scalefactor`) | `TSheetView::timeref_scalefactor` (public field), plus one copy in each of the three viewports (`TViewPortInterface::m_timeRefScaleFactor`) | `TSheetView::scale_factor_changed()` pushes it to all four | commands directly through `d->sv->timeref_scalefactor` (~15 places), `cpointer()` through the viewport copy, views |
| Horizontal scroll position | `TSession::m_scrollBarXValue` (saved), `TSheetWidget`'s visible `m_hScrollBar`, the clips viewport's hidden scrollbar, the ruler viewport's hidden scrollbar | legacy commands → `TSheetView::set_hscrollbar_value()` → `TSession`; the controller shuttle → the hidden viewport scrollbar directly; the wheel → `TSheetView::scroll_*_by()` | all of them, kept in sync by signal chains (§3.4) |
| Vertical scroll position | `TSession::m_scrollBarYValue` (saved), visible `m_vScrollBar`, hidden viewport scrollbars | as horizontal | as horizontal |
| Track heights | `TSession::m_trackHeights` (saved) | `TSheetView::vzoom()`, track resize | layout |
| Work cursor ("edit point") | `TSession::m_workLocation` | keyboard navigation (through the canvas cursor!), work cursor commands, touch | views, snapping, many commands |
| Canvas cursor position | `TCanvasCursor` scene item, `TMouseData::canvasCursorPos` in `cpointer()` | mouse hover, hold jog, keyboard navigation (animated), `cpointer()` | cursor renderers |

Pixel ↔ time is computed in at least four ways: `x * timeref_scalefactor` in
commands and views, `scene_x() * get_timeref_scale_factor()` in `cpointer()`,
`location / timeref_scalefactor` in `TSheetView`, and `TTimeRef(...)` casts.

## 3. Flows

### 3.1 Key press (no hold)
```
Qt KeyPress → TViewPort::keyPressEvent → ied().catch_key_press (auto repeat ignored)
  → process_press_event: Esc/Enter/hold key checks → number collection → modifiers
  → TShortCut for the key → cpointer().prepare_for_shortcut_dispatch()
        (snapshot: active context items, first-input-event positions)
  → dispatch_shortcut: context items (snapshot + related items + permanent items)
        → resolve per item (class chain, modifiers) → slot, command or controller
        → hold: begin_hold → set_holding(true) → cpointer().hold_start()
               (grab mouse, blank cursor) → holdStarted signal
```

### 3.2 Mouse move (no hold)
```
Qt MouseMove → TViewPort::mouseMoveEvent
  → set current viewport if none
  → cpointer().update_mouse_positions(pos, global)
        (not holding: only the keyboard-only takeover check)
  → filter: same position as before (scrollbar generated) → stop
  → detect_items_below_cursor(): scene items under the cursor + TSheetView
        → cpointer().set_active_context_items_by_mouse_movement
              → set_has_active_context(true/false) per item → contextChanged
                    → TSheetView::context_changed: cursor overlay letter =
                      second character of the class name
        → mouse tracking items: mouse_hover_move_event
              → TSheetView: canvas cursor follows the mouse
```

### 3.3 Mouse move during a hold
```
Qt MouseMove → TViewPort::mouseMoveEvent
  → filter: same global position as last time (shuttle generated) → stop
  → cpointer().update_mouse_positions → ied().jog()       ← the dispatcher is
        driven from cpointer, not from the viewport or an input layer
        → controller: shuttle start/stop, update(), canvas cursor follows
        → legacy: jog bypass, command->jog(), TMoveCommand::jog() (shuttle),
                  canvas cursor follows
  → TViewPort returns (no item detection while holding)
```

### 3.4 Scrolling: three paths that do not meet
1. **Legacy shuttle and scroll commands** (`TMoveCommand`): `TSheetView::set_hscrollbar_value()`
   → `TSession::set_scrollbar_x()` → `horizontalScrollBarValueChanged`
   → `TSheetView`: hidden clips scrollbar + visible scrollbar
   → visible `valueChanged` → `hscrollbar_value_changed()` → hidden clips scrollbar again
   → hidden clips scrollbar ↔ hidden ruler scrollbar (connected in `TSheetWidget`).
2. **Controller shuttle** (`TViewportScroller`, Split): dispatcher → `TViewPortInterface::scroll_horizontal()`
   → the clips viewport's **hidden** scrollbar directly → ruler follows. The visible
   scrollbar and `TSession`'s scroll position are **not updated**.
3. **Mouse wheel and trackpad**: `TViewPort::wheelEvent` → `TSheetView::scroll_*_by()` → path 1.
   The dispatcher's `catch_scroll()` is only reached when the wheel delta is zero.

### 3.5 Zoom
```
zoom command / trackpad gesture → TSheetView::hzoom → TSession::scale_hzoom
  → hzoomChanged (also to child sessions) → TSheetView::scale_factor_changed
  → timeref_scalefactor = hzoom × (UNIVERSAL_SAMPLE_RATE / 44100)
  → pushed into the three viewports → layout of all tracks
```

### 3.6 Keyboard navigation (arrow keys, context levels)
```
key → TSheetView::browse_to_* (string compare of the current context's class name)
  → browse_to_audio_clip_view etc.
  → set_canvas_cursor_location(time, sceneY)
        → TSession::set_work_at(time)      ← moves the WORK cursor (model state)
        → scroll if near the border, store the cursor position in cpointer()
        → animate the canvas cursor
  → cpointer().set_active_context_items_by_keyboard_input(items)
        → keyboard-only mode until the mouse moves N pixels
```

### 3.7 Context menu
```
key (ShowContextMenu) → TMainWindow::show_context_menu
  → items: holding command, or context items whose class name contains "View"
  → menu entry → ied().dispatch_shortcut_from_contextmenu
        → restore the global mouse position, processEvents()
        → re-dispatch the function's first key
```

## 4. Problems found

| # | Problem | Evidence | Kind |
|---|---|---|---|
| F1 | Time per pixel exists in 5 copies and is converted in 4 ways | §2 | design |
| F2 | Scroll position exists in up to 5 copies with 3 write paths; the controller shuttle bypasses the visible scrollbar and `TSession` | §3.4 path 2 | **probable bug**: after a Split shuttle the visible scrollbar and the saved position are stale |
| F3 | Mouse wheel shortcuts (e.g. Gain Increase/Decrease on the wheel) never reach the dispatcher | §3.4 path 3; `4cce2ece`, `1c05d416` (Ben, 2026-09: "will integrate it into the event dispatcher in the future") | **bug** |
| F4 | The dispatcher is driven from `cpointer().update_mouse_positions()` | §3.3 | flow |
| F5 | Keyboard navigation moves the work cursor through the canvas cursor | §3.6 | coupling (intent to confirm) |
| F6 | Keyboard navigation compares class names as text; `"CurveView"` should be `"TCurveView"` | `TSheetView::browse_to_next_context_item` | **bug**: next/previous curve node by keyboard does nothing |
| F7 | The cursor overlay letter is the second character of the class name | `TSheetView::context_changed` | naming as semantics |
| F8 | Track order is computed by the view from its view lists | `move_trackview_up/down` | model logic in view (known) |
| F9 | Two shuttle implementations (legacy `TMoveCommand`, controller `TViewportScroller`) with different scroll paths | §3.4 | duplication |
| F10 | `TSheetView` is ~1400 lines with at least 8 responsibilities | §1 | god class |
| F11 | View state (zoom, scroll, track heights) lives in the model (`TSession`, saved in the project) next to model state | §2 | intent to confirm |
| F12 | `leaveEvent` FIXME: a context menu leaves the viewport and can leave an empty context, crash on Enter | `TViewPort::leaveEvent` | known, unfixed |

## 5. Proposed split into roles

| Role | Owns | Replaces |
|---|---|---|
| **Input intake** | Qt events → normalized input (key/button/wheel/move/gesture with modifiers), one entry into the dispatcher, wheel as an input like any other | event handlers in `TViewPort`, `cpointer().update_mouse_positions → ied().jog()` |
| **Pointer** | mouse and keyboard pointer positions, keyboard-only mode and takeover, hold grab/blank cursor | position parts of `TContextPointer` |
| **Context** | items under the pointer (mouse or keyboard), snapshot at first input event, permanent items, menu items | context parts of `TContextPointer`, `detect_items_below_cursor` |
| **Timeline view** (view model) | zoom, time per pixel, scroll position, visible range, **the only pixel ↔ time conversion**; one write path; signals for views and QML | `timeref_scalefactor` copies, scrollbar sync chains, conversions in commands |
| **Navigation** | keyboard navigation between items and context levels, designed per domain (time, tracks, ordered chains such as plugins), on model/domain objects, no class name strings | `TSheetView::browse_to_*`, which was a quick prototype "to make it work" and is **not** a reference for the design (user, 2026-10-14) |
| **Canvas cursor = edit point** | the edit point: location in time and the item it is on, the reference for navigation and actions; drawn position (animated or not), type, data from the active tool or controller | canvas cursor parts of `TSheetView`, `TContextPointer`, `TViewPort`; navigation's use of the work cursor |
| **Track layout** | track heights, layout, order from the model owner | layout parts of `TSheetView`, `move_trackview_*` |

`TSheetView` would keep creating and wiring the views of a sheet; `TViewPort`
would only translate Qt events and paint.

## 6. Open questions (design intent)

1. Keyboard navigation sets the work cursor (F5): is the work cursor meant to
   be "the keyboard edit point"?
   **Answer (2026-10-14):** yes, but its usefulness is debatable and may be a
   user preference. **The canvas cursor is the ultimate edit point**, even
   though today it lacks that authority. Consequence for the design: the edit
   point (canvas cursor location in time and on which item) is owned by the
   input layer; keyboard navigation and actions use it as their reference
   (today `browse_to_*` uses the work location); moving the work cursor along
   becomes an optional user preference.
2. Zoom, scroll and track heights are saved in the project (F11): keep them as
   saved per-sheet view state (then they belong in a timeline view model that
   is saved with the sheet), or are they model state?
   **Answer (2026-10-14):** the intent was one XML file that holds the whole
   project state, including the view state; it is not model state. Consequence
   for the design: zoom, scroll and track heights move from `TSession` into the
   timeline view model of the sheet, which writes its own element into the same
   project file. Existing project files keep loading (same attributes, read by
   the view model).
