// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The kinds of ids core has, and their tags. Kind and subtype numbers are part
// of the project file: never renumber, never reuse one that was given up.
//
// A kind is what an id points at, as it is asked for: a track, a clip. A
// subtype is what it is and stays: audio or bus. A tag accepts a set of
// subtypes: one (exact, the only ones that make ids), a layer (a mixer track:
// audio or bus), or all of the kind.
//
// The classes are only declared here: the cast in TObjectPool::find() needs
// their definition where find() is called, not in this file.

#include "TObjectId.h"

#include <cstdint>

class TTrack;
class TAudioMixerTrack;
class TAudioTrack;
class TBusTrack;
class MasterOutSubGroup;
class TBounceTrack;
class TClip;
class TAudioClip;
class TSheet;

// Kind 0 is no kind
enum : uint8_t {
    TrackKind = 1,
    ClipKind = 2,
    SongKind = 3,
};

// The masters and the bounce track have their own subtype: an agent and the
// pool can tell them apart by the id alone
enum : uint8_t {
    AudioTrackSubtype = 1,
    BusTrackSubtype = 2,
    SongMasterSubtype = 3,
    ProjectMasterSubtype = 4,
    BounceTrackSubtype = 5,
};

enum : uint8_t {
    AudioClipSubtype = 1,
};

enum : uint8_t {
    SongSubtype = 1,
};

// Tracks: TrackId > AudioMixerTrackId > AudioTrackId, BusTrackId, the two
// masters, BounceTrackId. AudioMixerTrackTag's Object is declared only until
// the split of TTrack (the next branch); nothing finds by it before then

struct TrackTag
{
    static constexpr uint8_t Kind = TrackKind;
    static constexpr uint64_t Subtypes = TObjectIds::AllSubtypes;
    static constexpr const char* Name = "track";
    using Object = TTrack;
};

struct AudioMixerTrackTag
{
    static constexpr uint8_t Kind = TrackKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(AudioTrackSubtype) | TObjectIds::subtype(BusTrackSubtype)
                                         | TObjectIds::subtype(SongMasterSubtype) | TObjectIds::subtype(ProjectMasterSubtype)
                                         | TObjectIds::subtype(BounceTrackSubtype);
    static constexpr const char* Name = "mixertrack";
    using Object = TAudioMixerTrack;
};

struct AudioTrackTag
{
    static constexpr uint8_t Kind = TrackKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(AudioTrackSubtype);
    static constexpr const char* Name = "audiotrack";
    using Object = TAudioTrack;
};

struct BusTrackTag
{
    static constexpr uint8_t Kind = TrackKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(BusTrackSubtype);
    static constexpr const char* Name = "bustrack";
    using Object = TBusTrack;
};

// The masters and the bounce track belong one to one to their owner in a fixed
// role: their id is derived from it (TObjectFactory::make_owned())

struct SongMasterTag
{
    static constexpr bool FromOwner = true;
    static constexpr uint8_t Kind = TrackKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(SongMasterSubtype);
    static constexpr const char* Name = "songmaster";
    using Object = MasterOutSubGroup;
};

struct ProjectMasterTag
{
    static constexpr bool FromOwner = true;
    static constexpr uint8_t Kind = TrackKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(ProjectMasterSubtype);
    static constexpr const char* Name = "projectmaster";
    using Object = MasterOutSubGroup;
};

struct BounceTrackTag
{
    static constexpr bool FromOwner = true;
    static constexpr uint8_t Kind = TrackKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(BounceTrackSubtype);
    static constexpr const char* Name = "bouncetrack";
    using Object = TBounceTrack;
};

// Clips: ClipId > AudioClipId. ClipTag's Object is TAudioClip until TClip
// exists (after the composition of the processing node)

struct ClipTag
{
    static constexpr uint8_t Kind = ClipKind;
    static constexpr uint64_t Subtypes = TObjectIds::AllSubtypes;
    static constexpr const char* Name = "clip";
    using Object = TAudioClip;
};

struct AudioClipTag
{
    static constexpr uint8_t Kind = ClipKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(AudioClipSubtype);
    static constexpr const char* Name = "audioclip";
    using Object = TAudioClip;
};

using TrackId = TObjectId<TrackTag>;
using AudioMixerTrackId = TObjectId<AudioMixerTrackTag>;
using AudioTrackId = TObjectId<AudioTrackTag>;
using BusTrackId = TObjectId<BusTrackTag>;
using SongMasterId = TObjectId<SongMasterTag>;
using ProjectMasterId = TObjectId<ProjectMasterTag>;
using BounceTrackId = TObjectId<BounceTrackTag>;
// Songs: one subtype. In the code a song is still a TSheet; a session (a
// container of tracks, to be named workspace) has no id yet

struct SongTag
{
    static constexpr uint8_t Kind = SongKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(SongSubtype);
    static constexpr const char* Name = "song";
    using Object = TSheet;
};

using ClipId = TObjectId<ClipTag>;
using AudioClipId = TObjectId<AudioClipTag>;
using SongId = TObjectId<SongTag>;

// At start-up, before anything logs an id. Only exact tags: ids are made with
// those, so they are the ones the log shows
inline void register_object_id_names()
{
    TObjectIds::register_name(TObjectIds::make_tag(TrackKind, AudioTrackSubtype), AudioTrackTag::Name);
    TObjectIds::register_name(TObjectIds::make_tag(TrackKind, BusTrackSubtype), BusTrackTag::Name);
    TObjectIds::register_name(TObjectIds::make_tag(TrackKind, SongMasterSubtype), SongMasterTag::Name);
    TObjectIds::register_name(TObjectIds::make_tag(TrackKind, ProjectMasterSubtype), ProjectMasterTag::Name);
    TObjectIds::register_name(TObjectIds::make_tag(TrackKind, BounceTrackSubtype), BounceTrackTag::Name);
    TObjectIds::register_name(TObjectIds::make_tag(ClipKind, AudioClipSubtype), AudioClipTag::Name);
    TObjectIds::register_name(TObjectIds::make_tag(SongKind, SongSubtype), SongTag::Name);
}
