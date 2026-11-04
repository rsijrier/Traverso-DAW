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
| Sequence | 1 to 2^48 − 1 | Per project, increasing from 1 |

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
| Every id unique | The first keeps it; the next gets a new id; references stay with the first; warning |
| Every id's tag fits its element | New id; warning |
| Every reference resolves | Reference dropped; warning |
| No old-style id attribute left | Error in a converted project: the conversion missed it |
| Next number above the highest id | Corrected by `restore()` |
| No id near 2^48 | Refuse to load; error |

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

## Related

- Composition of `TAudioProcessingNode` (separate branch, before `TClip`):
  `ClipTag::Object` is `TAudioClip` until then, `TClip` after.
- Concepts for tools, edits and the virtual cursor: the ids are what edits,
  JSON and agent tests address objects by.
