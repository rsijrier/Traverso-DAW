<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# TSplitEdit

Step 4 of Remon's brainstorm "Traverso begrippen voor tools, edits en de
virtuele cursor" (2026-11-04): one thin path, an edit with a test, as the
test of the concepts. Decisions of 2026-11-07 (Remon, after the analysis and
Fable's review).

## Order

1. `TTimeLineEntity::Type` goes (done first, on branch `split-edit`).
2. `TSplitEdit`.
3. Later, each its own step: the JSON call with the project UUID; the link
   cycle (T108) and the edit test with a real sheet; `TSplitTool` on the
   virtual cursor.

## Shape

| Piece | What |
|---|---|
| `TSplitEdit::apply(ClipId[], TTimeRef[])` | GUI thread. Looks the ids up in the project's pool, validates every clip before any change is computed, collects the changes, has the sheet make one history step. Knows no kind of clip and no selection |
| `TClipChange` | the clips that come and go, as `TClip*`, computed by the clip |
| `TClip::calculate_split_change(positions)` | virtual, replaces `validate_split_boundary()` and `calculate_split_mutation()`; n positions inside the clip give n + 1 pieces |
| a pure function | start, end, source start and positions → the n + 1 pieces (location, source range); the audio clip applies them to its copies; this is what the test compiles |
| `TSheet::create_command_mutate_clips()` | stays; takes the change. The one narrowing, `qobject_cast<TAudioClip*>` with an assert, sits here, the same form as the pool's `static_cast`; it goes when a track holds `TClip*` (for instance when text clips come) |
| `create_command_split_entities()` | goes |
| `TUndoCommand` | stays; the name `THistoryStep` comes later, in one go |

## Validation, before anything is computed

The copies add read sources to the disk io when they are computed
(`TAudioClip::calculate_split_mutation()`, `TResourcesManager::get_clip()`),
so nothing is computed for a clip that is refused.

- the clip is in use: `find_in_project()`, built with `TSplitEdit` (Remon,
  2026-11-07; proposed on 2026-11-04, when the pool came to hold every living
  object). One lookup for edits, JSON and the agent: on `TProject`, not the
  pool (the pool knows no tree), `pool.find(id)` plus the question of
  `id_owner()`; nullptr for an object that is not in the project, such as a
  removed clip on the undo stack. To settle in the build: a song has no
  owner (`TSheet::id_owner()` is nullptr), so for songs the question is
  another one;
- it is not recording (T107);
- its track is in a song of the project (T96: clips of a removed track
  still count as in use);
- all clips are in one sheet: one history step belongs to one undo stack;
- the positions are sorted and without duplicates before a clip gets them;
- the positions are valid for the clip.

What cannot be split is skipped; the result names each skipped clip by its id
with the reason, so an agent or a JSON test sees exactly what did not happen.
An error when nothing splits.

## The test

The link cycle (T108) blocks a test with a real sheet. Now: the pure function
of positions → pieces in a test (tests/curves pattern), the rest at run time
by Remon.

## Recorded exceptions

- The old `TSplitEntityController` takes the `ClipId`s from the selection,
  which holds `TTimeLineEntity*`: a `dynamic_cast<TClip*>` there, which goes
  with `TSplitTool` and a selection of ids.
- The copies reach the clip's own project's resources manager
  (`get_sheet()->get_project()->get_audiosource_manager()`, as `id_owner()`),
  not the global `resources_manager()`.
