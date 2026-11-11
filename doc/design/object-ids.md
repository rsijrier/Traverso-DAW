<!--
SPDX-FileCopyrightText: 2026 Remon Sijrier
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Object ids

Decided 2026-11-04. Replaces the time-based id design (`TId`, `TIds`, 44 bits of
milliseconds); none of that format was ever released.

## Summary

An object id is 64 bits: a tag that says what the object is, and a sequence
number per project. Ids are unique within one project by construction, increase
in creation order, and are the same for the same steps in a test. No clock.

```
[ 8 bits kind | 8 bits subtype | 48 bits sequence ]
```

| Field | Range | Meaning |
|---|---|---|
| Kind | 1–255; 0 = no kind | What the id points at, as it is asked for: track, clip, curve, marker… |
| Subtype | 0–63; the top 2 bits of the byte are reserved, always 0 | What the object is and stays: audio, bus. Never changes during an object's lifetime |
| Sequence | 1 to 2^48 − 1 | Per project, increasing from 1; 0 only in an id derived from the project (its master) |

The value 0 is the invalid id. The fields are byte-aligned on purpose: the hex
form reads directly (`0301000000000005` = kind 3, subtype 1, number 5: an
audio track).

## Rules

1. Ids are unique **per project**, not across projects. Across projects, an
   object is (project UUID, id).
2. The order of ids is the order objects came into this project. An imported
   object gets a new, higher id. Nothing else may be read into the order.
3. Ids carry no time. Where an object needs a creation time, it gets its own
   `created` field.
4. Ids are made in the GUI thread, never in the real-time path. The audio thread
   that needs a new object asks the GUI thread through the message queue.
5. Kind and subtype numbers are part of the project file. The kinds follow
   the tree (project, song, track, clip, curve, marker), so an id reads as
   where it stands in it. Renumbered once on 2026-11-07 (Remon), when only
   development projects held ids: their load check gives a new id with a
   warning where a tag no longer fits its element, and a save makes them
   clean. **Frozen at the foundations release:** from then on never renumber
   them, never reuse a number that was given up.
6. In JSON and QML an id is always a string (`"audiotrack:0301000000000005"`):
   JavaScript numbers hold only 53 bits exactly.

## Tags

A tag struct accepts a **set** of subtypes of one kind, as a mask:

```cpp
struct AudioMixerTrackTag
{
    static constexpr uint8_t Kind = TrackKind;
    static constexpr uint64_t Subtypes = subtype(AudioTrackSubtype) | subtype(BusTrackSubtype);
    static constexpr const char* Name = "mixertrack";
    using Object = TAudioMixerTrack;
};
```

| Mask | Meaning | Example |
|---|---|---|
| One subtype | Exact; the only tags that can `create()` | `AudioTrackId`, `BusTrackId` |
| Several | A layer | `AudioMixerTrackId` (audio or bus) |
| All | Any object of the kind | `TrackId` |

An id converts implicitly to a tag whose set holds its own:
`AudioTrackId` → `AudioMixerTrackId` → `TrackId`. Never the other way, never
across kinds. The compiler enforces this.

`Tag::Object` is the class `TObjectPool::find()` returns, through a
`static_cast` from `QObject*`. Every `Tag::Object` is therefore a `QObject`.

The value of an id is an `object_id_value_t`. Only the project file and the
pool's index see it (`TObjectFactory`, `TObjectPool`, and the tests through
`TObjectIdTestAccess`): `from_storage()`, `to_storage()` and `accepts()` are
private. Everyone else holds a `TObjectId`: as a `QHash` key (`qHash()`), as
item data in a drag (`QDataStream`; a value of another tag reads as the
invalid id), and as text (`to_string()`, `to_hex()`). The project's own id is
`TObjectId::root()`: its exact tag on number 0.

## Generator

One `TObjectIdGenerator` per project, owned by the project (no singleton).

```cpp
class TObjectIdGenerator
{
public:
    object_id_value_t create(uint16_t tag);                     // mutex, m_next++
    void restore(object_id_value_t savedNext, object_id_value_t highestLoaded);   // at load
    object_id_value_t next() const;                             // for save
};
```

- `restore()` sets the next number to max(saved next, highest loaded + 1), so a
  hand-edited or missing counter can never hand out an existing id.
- `create()` refuses (asserts and logs) when the sequence reaches 2^48.
- A mutex, not lock-free: simpler, and ids are never made in the real-time path.

## Project file

| Attribute | Where | Why |
|---|---|---|
| Project UUID | Project root | Names the id space; new UUID on "Save as" |
| Next sequence number | Project root | `restore()` |
| Object ids under a **new attribute name** (not `id`) | Every element, and every attribute that refers to another object | Old and new ids are told apart by name, not by bits |

All references get a new name too, not only the object's own id: `clip_id`,
`bus_id`, `track` and the like.

## Load validation

One function, run on every load. It is also the test for the conversion of old
projects.

| Check | On failure |
|---|---|
| Every id unique, and every counter number used once | The first keeps it; the next gets a new id; references stay with the first; warning |
| Every id's tag fits its element | New id; warning |
| Every reference resolves | Reference dropped; warning |
| No old-style id attribute left | Error in a converted project: the conversion missed it |
| Next number above the highest id | Corrected by `restore()` |
| No id within 2^32 of the end of the sequence | Damaged: new id; warning |

## Old projects

Recognised by the project file version. Converted once on load: every old id
gets a new one, every reference follows through an old → new table. The original
file is kept as a copy (`<name>.pre-<version>`). An older Traverso opening a
converted project says so clearly instead of failing silently.

## Edit files and logs

Edit files (JSON from an agent or a test) and logs carry the project UUID. An
edit file for another project is refused. This is the one real risk of a
per-project counter: ids of different projects look alike.

## Removed

`system_clock`, `steady_clock`, the epoch, the random counter start, waiting on
the clock, the 44-bit time field, the reservation of kinds for old ids. Only
kind 0 stays reserved, as "no kind".

## Tests

- The same steps give the same ids (deterministic).
- Ids increase in creation order.
- After load, a new id is above the highest id in the file, also with the
  counter lowered or removed by hand.
- Conversions: `AudioTrackId` → `AudioMixerTrackId` → `TrackId` compile; the
  reverse, across kinds, `create()` on a layer or `pool.add()` with a layer id
  do not.
- Load validation: one case per row of its table.
- Converted old project: no old-style attribute left, all references resolve.
- An edit file with another project UUID is refused.

## Derived ids

An object that belongs one to one to an owner, in a fixed role, gets no number
from the counter: its id is derived from the owner.

| Rule | Id |
|---|---|
| Exactly one per owner per role, and the object never changes owner | derived |
| More than one per owner, or it can change owner | counter |

| Object | Id |
|---|---|
| Song master, project master, bounce track | derived (owner: song or project) |
| Gain curve, fade-in, fade-out of a clip or track (gain curves with T108; the bounce track has none) | derived (owner: clip or track) |
| Ruler | derived (owner: song) |
| Plugin chain | derived (owner: clip or track) |
| Tracks, clips, plugins, markers, sends, automation lanes per parameter | counter |

Traverso has no pan curve; a clip has three curves (gain, fade-in, fade-out)
and a plugin chain, like a track.

```
derived id = [ kind of the role | subtype of the role | the owner's sequence number ]
```

- Every role has its own subtype, also when the class is the same: fade-in and
  fade-out are one class, `TFadeCurve`, with two subtypes. A tag with a mask
  over both is allowed.
- The project master has number 0: the project has no number; the id is valid,
  its tag is set.
- The 48 bits are not divided further: the number has one meaning.
- Unique: only one role uses a subtype, and each owner's number is unique. A
  derived id never meets one from the counter, its subtype is its own.
- One sequence for all kinds: a clip 7 and a track 7 never exist together, so
  `[5|1|7]` names one curve. The factory asserts it where it adds a counter id
  (a number used twice) and where it adds a derived one (an id made twice).
- An owner that is itself derived shares its number: the song master and the
  bounce track both have the song's number `s`. Where two derived owners share
  a number, their derived objects in the same role need different subtypes.
  The bounce track has no gain curve (Remon 2026-11-11), so no gain curve
  meets this now. `tests/core/test_derived_ids.cpp` builds every kind of
  derived object next to counter owners and checks that no id is made twice.
- A derived object has no function to change its owner.

In branch `object-ids` the masters and the bounce track are derived; the
curves, the ruler and the plugin chains get their ids, derived, in a next step,
with this mechanism.

## The factory

`TObjectFactory` (`src/core/TObjectFactory.h`) is the one place where objects
with an id are made. The project owns the sequence, the pool and the factory:
`project->objects().make<TAudioTrack>(sheet, name, height)`.

| Function | Makes |
|---|---|
| `make<T>(args...)` | a new object, a new number from the counter |
| `load<T>(element, args...)` | an object from its element, with the id saved there when it fits and is free; else a new one, with a warning |
| `make_derived<Tag>(owner, args...)` | static, no pool: a derived object, in its owner's constructor, as `Tag::Object(id, owner, args...)`, the id from `owner->object_id()`. Refuses at compile time an owner whose own id is derived (its `IdTag` is `FromOwner` or `void`): an object of such an owner gets a counter id |

A class with an id has `using IdTag = AudioTrackTag;` (`void` for a class in
more than one role: its maker names the tag), a private `m_objectId`,
and `friend class TObjectFactory`. The factory decides the id before it builds the object: a class whose
constructor takes its id first gets it there (a song, `TSheet(SongId, ...)`,
which makes its master and bounce track from its own number), the others right
after. A derived peer (a master, the bounce track) is enrolled by its maker
with `TObjectPool::add<Tag>(object)` (the song for its master and bounce track,
the project for its master); a derived part is not enrolled.

Every `new` of a track, clip or song goes through the factory. An object made
around it has no id, and `object_id()` asserts on first use.

## Peers and parts

The rule for the pool is "peer or part", not "derived or counter" (Remon,
2026-11-11), a property of the tag: `InPool`.

- Peers are in the pool: song, track (with the masters and the bounce track),
  clip, marker, send, source, workspace, plugin. The masters are derived but
  peers: a send points at a master. `find()` reaches every living peer by a
  bare id (QML, the agent, JSON).
- Parts are not in the pool: gain, fade and automation curves, the ruler, the
  plugin chain. Their address in edits, JSON and undo is (owner id, role), not
  their id: a part is found through `pool.find(owner)` and the owner's
  accessor, so an edit can never go around the owner.
- `TObjectPool::add()` and `find()` compile only for a tag with `InPool`. The
  project is not in its own pool (the pool is its member).

## The pool

`TObjectPool` (`src/core/TObjectPool.h`), owned by the project, two indexes:

| Index | Holds |
|---|---|
| byId | every peer, by its id |
| byNumber | the peers with a counter id, by their number |

`find(id)` gives a peer of the tag. A derived peer is in byId only: its
number is its owner's. An object leaves the
pool when it is destroyed (`QObject::destroyed`): no empty entries, so
`contains()` is never true for a dead object. Whether an object is in the
project's tree now is not the pool's question: an edit asks the object (is this
clip on a track, is that track in a song of the project) and refuses with a
reason. A removed track or clip stays findable as long as it lives, for undo.

A derived object always hangs on the owner its number names: `make_derived()`
is given the owner and has no other way in, and a derived object has no
function to change its owner. So there is no check of it (prevent, not detect;
Remon, 2026-11-08).

| Kind | Subtype | Tag | Object | Id |
|---|---|---|---|---|
| 1 project | 1 | `ProjectTag` | `TProject` | fixed, number 0 (`TObjectId::root()`): `project:0101000000000000`, not saved |
| 2 song | 1 | `SongTag` | `TSheet`: a sheet is a song; owner the project, not in the file | counter |
| 3 track | all | `TrackTag` | `TTrack` | |
| | 1 to 5 | `AudioMixerTrackTag` | `TAudioMixerTrack` (from the next branch) | |
| | 1 | `AudioTrackTag` | `TAudioTrack` | counter |
| | 2 | `BusTrackTag` | `TBusTrack` | counter |
| | 3 | `SongMasterTag` | `MasterOutSubGroup` of a song | derived |
| | 4 | `ProjectMasterTag` | `MasterOutSubGroup` of the project | derived, number 0 |
| | 5 | `BounceTrackTag` | `TBounceTrack` | derived |
| 4 clip | all | `ClipTag` | `TClip` | |
| | 1 | `AudioClipTag` | `TAudioClip` | counter |
| 5 curve | all | `CurveTag` | `TContextItem` (`TCurve` and `TFadeCurve` share no other base) | |
| | 1 | (no tag yet) | `TCurve` of a clip's or track's fader | derived `[5|1|owner]` (project master `[5|1|0]`); with T108 |
| | 2 | `FadeInCurveTag` | `TFadeCurve` | derived from its clip |
| | 3 | `FadeOutCurveTag` | `TFadeCurve` | derived from its clip |
| | 4 | (no tag yet) | an automation `TCurve` of a plugin port | counter, saved; with T108 |
| | 5 | not used | | the bounce track has no gain curve |
| 6 marker | 1 | | `TTimeLineMarker` | counter, owner its ruler |
| 7 send | 1 | | `TSend` | counter, owner its track; its target is a track |
| 8 audio source | 1 | | the source (resources manager) | counter, owner the project |
| 9 workspace | 1 | | a song's child session | counter, owner its song |
| 10 ruler | 1 | | `TTimeLineRuler` | derived from its song, the owner of the markers |
| 11 plugin chain | 1 | | `TAudioPluginChain` | derived from its track or clip; after T108 |
| 12 plugin | LV2, VST3, CLAP, internal | | `TAudioPlugin` | counter, owner its chain (several, movable); after T108 |

No object id (Remon, 2026-11-08): `AudioBus` (the engine's; in the model the
bus track is the object, hardware is named by name or port), `AudioChannel`
(out of the file: it is never read), a curve node (local in its curve:
(`CurveId`, number)). The numbers of 11 and 12 are fixed now, so nothing
shifts later.

## Version 4 of the project file (Remon, 2026-11-08)

Decided for the step that replaces the old ids:

- **No interim loading.** The loaders read only the new references; every
  version 3 file is converted to version 4 first. No load code for old files.
- **Converter 3 → 4**, after the existing 2 → 3: converted on load, the
  original kept as `<file>.pre-4` (`-2`, `-3` … when that exists). Templates
  are converted in memory; a restored backup is converted at its load. Every
  object gets a new id, numbered from 1 in document order; no owner is
  written (the first save adds them). A file with an old `id` left after the
  conversion is not loaded. A file of a newer version is refused with its own
  message. The conversion writes the backup and the converted file next to
  the original: a version 3 project on storage that cannot be written (read
  only, a full disk) is refused, where version 3 loaded it before.
- **`createdon`**: the project's creation date and time, ISO 8601 in UTC, on
  `Properties`; filled by the converter from the old project id, written by a
  new project when it is made and by a project made from a template. Shown in
  local time. A version 3 project shows no date in the open dialog until it
  has been opened once.
- **Project UUID**: `uuid` on `Properties`, made once when a project is made;
  the converter gives an old project one, a project made from a template gets
  its own. Comes in this step (the
  take names need it), before the JSON step.
- **Take file names**: `<project>-<song>-<track>-<take>.<extension>`, with
  the first 12 hex digits of the project UUID, the sequence numbers of song
  and track in 5 hex digits each, and the take number in 4 decimal digits:
  `3f9a1c2e7b04-00005-00012-0003.wav` (34 characters). No word "take" and no
  source id: the folder and the source say it is a take, and the take number
  is saved per track and only grows, so project, song, track and take are
  unique. The extension follows the recording format. The fixed widths sort a
  list by project, song, track and take; a number past its width makes its
  field longer, the name stays unique. The source gets its id before the file
  is made. An existing file is never overwritten. Old recordings keep their
  names. The short name a source shows is saved in its element, so no rule
  derives it from the file name.
- **Origin of a take**: `originsong` (a `SongId`) on the source of a
  recording, next to `wasrecording`; not on imports. A clip's song follows from
  its owner (clip → track → song); a take clip in the library finds its song
  through its source. A clip's `sheet` and a source's `origsheetid` go.
- **A send's target**: two attributes, `targettrack` (a `TrackId`) and
  `targetport` (a port by name), exactly one of them; not one string read two
  ways. The only software port in the file is `jackmaster`: the software
  buses (a track's own jack buses and the jack master) are not saved, they are
  made again when the project and its tracks connect to jack, with their
  sends.
- **The active session**: `currentsong` (a `SongId`) and, when a workspace is
  active, `activeworkspace` (a `WorkspaceId`). The project itself as the
  active session is not saved: the load opens the current song.
- **Channels**: `AudioChannels` and a bus's `channelids` leave the file.

## Saved now, next to the old ids

Branch `object-ids` saves the ids under their own attribute names, next to the
old `id`: an older Traverso ignores them, the project file version stays. In a
file an id is bare hex as a string; the prefix (`audiotrack:`) is for the log,
the screen and the agent. A prefix met at reading is checked against the id's
tag and then ignored.

| Attribute | Where |
|---|---|
| `objectid` | every object with a counter id: track, clip, song (`Sheet`), marker, send. Not in templates (`TObjectFactory::remove_ids()` on a template's song or project). The masters still write theirs (derived: only as a check), until the rule below reaches them. The bounce track is not saved |
| `owner` | every object with a counter id and an owner: a track its song, a clip its track, a marker its ruler, a send its track. Not the project master, not a clip on no track (also not a removed clip that keeps its track for undo) |

**Derived objects write no owner and no id** (Remon, 2026-11-08): their id
follows from their owner, and the structure of the file places them, so there
is nothing left to check. The ruler is the first: it writes neither. The
masters follow in a step of their own.

The markers of a CD export list (`TTimeLineRuler::get_cdtrack_list()`, the
export thread) are made without the factory and have no id: they never join
the project and are never asked for one (rule 4: ids only in the GUI thread).
| `nextobjectid`, a number | the project's `Properties` |

The structure of the file places an object, as the loader does now: a track by
the song it is in, a clip by the track whose clip list names it, a master by its
song. **`owner` is written, not read or checked** (Remon, 2026-11-08): the
structure places an object, and nothing in the loader needs it. A flat file
(T112), where nothing is nested, will place objects by it. Ids at load:

| Case | Behaviour |
|---|---|
| An `objectid` that does not fit its element, is used twice, has a number another object has, or lies within 2^32 of the end of the sequence | a new one, a warning; the first loaded keeps it |
| A damaged `nextobjectid`: no number, or within 2^32 of the end | ignored, a warning |

Nothing here refuses to load: such an id can only come from a damaged file,
and it is treated like any other damaged id.

**Order at load:** before anything is made, `restore()` takes the saved next
number and the highest number of any `objectid` in the file (ids within 2^32 of
the end left out). An object made during the load, because its id is missing or
damaged, then never takes a number an element further on has.

References between objects other than `owner` stay on the old ids for now: a
send's `bus` attribute is the old id of the bus track it sends to (the default
send of every track goes to its song's master, a song master's to the project
master). Their conversion to the new ids, the project UUID and the conversion
of old projects come later (T14, T42).

## Related

- Composition of `TAudioProcessingNode` (separate branch, before `TClip`,
  merged 2026-11-06); `ClipTag::Object` is `TClip` since branch `tclip`.
- Concepts for tools, edits and the virtual cursor: the ids are what edits,
  JSON and agent tests address objects by.
