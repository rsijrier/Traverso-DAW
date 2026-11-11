<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Code built ahead of its users

Code built before it has a caller, on purpose: so later work uses the
mechanism instead of patching around it. Each row names the ticket that
will call it and a limit, a milestone, at which the row is settled: in use,
or the code removed. Status: waiting / in use / removed. A caller only in a
test does not count.

Checked against `master` at `9aa3cdfe` (the merge of `new-ids`), 2026-11-11.

| Concept | Files | Planned user (ticket) | Limit | Status |
|---|---|---|---|---|
| `TObjectPool::find()` (and `find_raw()`): an object by its id | `src/core/TObjectPool.h` | T119: getters by id through the pool | merge of T119 | waiting (callers only in tests) |
| `TObjectPool::remove()` | `src/core/TObjectPool.h` | T119: the state "removed, held by undo" | merge of T119 | waiting (callers only in tests) |
| `InPool` on the tags (peer or part) | `src/core/TObjectIdTags.h`, `src/core/TObjectPool.h` | T119: `find()` reaches every peer by a bare id; today it only guards `add()` | merge of T119 | waiting |
| `AudioMixerTrackTag` (set tag: audio and bus tracks) | `src/core/TObjectIdTags.h` | T95: the `TTrack` split, `TAudioMixerTrack` | merge of T95 | waiting (no id of this tag is used) |
| `owner` attribute in the project file (`TObjectFactory::write_owner()`): written, never read | `src/core/TObjectFactory.h`; writers in `TTrack.cpp`, `TSend.cpp`, `TTimeLineMarker.cpp`, `TAudioClip.cpp`, `TWorkspace.cpp` | T112: the flat project file places objects by their owner | merge of T112 | waiting |
| Reserved numbers without a tag: kinds 11 plugin chain, 12 plugin; curve subtypes 1 gain, 4 automation; the plugin subtypes | `src/core/TObjectIdTags.h`, `doc/design/object-ids.md` | T108: gain curve ids, plugins | merge of T108 | waiting |
| `TProjectFileConversion3To4`: v3 project files to v4 on load | `src/core/TProjectFileConversion3To4.h/.cpp`, `tests/projectfile/` | `TProject::load()` (every v3 file, template and backup) | one release after v4 | in use (temporary by nature) |
| `TObjectPool::id_of()` (an object's id through the pool) | `src/core/TObjectPool.h` | none: every object with an id has `object_id()`; its one user, the owner check, was removed | — | removed (Remon, 2026-11-11) |
| `std::hash<TObjectId>` | `src/support/TObjectId.h` | none: the code uses `qHash`; comes back with a Qt-free library that needs it, as its user | — | removed (Remon, 2026-11-11) |
