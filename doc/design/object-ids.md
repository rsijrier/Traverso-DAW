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
form reads directly (`0101000000000005` = kind 1, subtype 1, number 5).

## Rules

1. Ids are unique **per project**, not across projects. Across projects, an
   object is (project UUID, id).
2. The order of ids is the order objects came into this project. An imported
   object gets a new, higher id. Nothing else may be read into the order.
3. Ids carry no time. Where an object needs a creation time, it gets its own
   `created` field.
4. Ids are made in the GUI thread, never in the real-time path. The audio thread
   that needs a new object asks the GUI thread through the message queue.
5. Kind and subtype numbers are part of the project file: never renumber them,
   never reuse a number that was given up.
6. In JSON and QML an id is always a string (`"audiotrack:0101000000000005"`):
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

## Generator

One `TObjectIdGenerator` per project, owned by the project (no singleton).

```cpp
class TObjectIdGenerator
{
public:
    uint64_t create(uint16_t tag);                              // mutex, m_next++
    void restore(uint64_t savedNext, uint64_t highestLoaded);   // at load
    uint64_t next() const;                                      // for save
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
| Gain curve, fade-in, fade-out of a clip or track | derived (owner: clip or track) |
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
| `make_owned<T, Tag>(ownerNumber, args...)` | a derived object, in its owner's constructor, with its final id |

A class with an id has `using IdTag = AudioTrackTag;` (`void` for a class in
more than one role: its maker names the tag), a private `m_objectId`,
`friend class TObjectFactory`, and `QObject* id_owner() const`: its owner now.
The factory decides the id before it builds the object: a class whose
constructor takes its id first gets it there (a song, `TSheet(SongId, ...)`,
which makes its master and bounce track from its own number), the others right
after. The owner gives its number to `make_owned()`, as it is not in the pool
yet while it runs its constructor.

Every `new` of a track, clip or song goes through the factory. An object made
around it has no id, and `object_id()` asserts on first use.

## The pool

`TObjectPool` (`src/core/TObjectPool.h`), owned by the project, two indexes:

| Index | Holds |
|---|---|
| byId | every object, by its id |
| byNumber | the objects with a counter id, by their number |

`find(id)` gives an object of the tag; `find_owner(id)` the owner of a derived
id: its number looked up in byNumber, the project for 0. An object leaves the
pool when it is destroyed (`QObject::destroyed`): no empty entries, so
`contains()` is never true for a dead object. Whether an object is in the
project's tree now is not the pool's question: an edit asks the object (is this
clip on a track, is that track in a song of the project) and refuses with a
reason. A removed track or clip stays findable as long as it lives, for undo.

**Invariant:** for every derived object, `pool.find_owner(object->id()) ==
object->id_owner()` (`owners_out_of_place()`). Checked at the end of every load
(a warning per object, and a `Q_ASSERT` in debug builds), and in the edit and
undo tests once there are edits (`TSplitEdit`).

| Kind | Subtype | Tag | Object | Id |
|---|---|---|---|---|
| 1 track | all | `TrackTag` | `TTrack` | |
| | 1 to 5 | `AudioMixerTrackTag` | `TAudioMixerTrack` (from the next branch) | |
| | 1 | `AudioTrackTag` | `TAudioTrack` | counter |
| | 2 | `BusTrackTag` | `TBusTrack` | counter |
| | 3 | `SongMasterTag` | `MasterOutSubGroup` of a song | derived |
| | 4 | `ProjectMasterTag` | `MasterOutSubGroup` of the project | derived, number 0 |
| | 5 | `BounceTrackTag` | `TBounceTrack` | derived |
| 2 clip | all | `ClipTag` | `TAudioClip` until `TClip` exists | |
| | 1 | `AudioClipTag` | `TAudioClip` | counter |
| 3 song | 1 | `SongTag` | `TSheet`: a sheet is a song. A session, a container of tracks (to be named workspace), has no id yet | counter |

## Saved now, next to the old ids

Branch `object-ids` saves the ids under their own attribute names, next to the
old `id`: an older Traverso ignores them, the project file version stays. In a
file an id is bare hex as a string; the prefix (`audiotrack:`) is for the log,
the screen and the agent. A prefix met at reading is checked against the id's
tag and then ignored.

| Attribute | Where |
|---|---|
| `objectid` | every track, clip and song (`Sheet`) element, also the masters (derived: only as a check); not in templates. The bounce track is not saved |
| `owner` | every object with an owner: a track its song, a clip its track, a master its song. Not the project master, not a clip on no track (also not a removed clip that keeps its track for undo) |
| `nextobjectid`, a number | the project's `Properties` |

The structure of the file places an object, as the loader does now: a track by
the song it is in, a clip by the track whose clip list names it, a master by its
song. `owner` and a derived `objectid` only check it:

| Case | Behaviour |
|---|---|
| No `owner` attribute | legitimate (a clip on no track): silent |
| `owner` names an id that does not exist | damaged or edited by hand. Counter object: it stays, the reference is dropped, a warning. Derived object: it goes, with a warning (does not occur yet: the masters are placed by the structure) |
| `owner` names another object than the structure | the structure wins, a warning |
| A derived `objectid` that does not follow from the owner | the owner wins, the id is derived again, a warning |
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

- Composition of `TAudioProcessingNode` (separate branch, before `TClip`):
  `ClipTag::Object` is `TAudioClip` until then, `TClip` after.
- Concepts for tools, edits and the virtual cursor: the ids are what edits,
  JSON and agent tests address objects by.
