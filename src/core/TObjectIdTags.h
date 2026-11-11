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

#include <QMetaType>

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
class TProject;
class TTimeLineRuler;
class TTimeLineMarker;
class TSend;
class TBufferedAudioStreamReader;
class TWorkspace;
class TContextItem;
class TFadeCurve;

// Kind 0 is no kind. The numbers follow the tree, so an id reads as where it
// stands in it (doc/design/object-ids.md). All are fixed, also those whose
// objects get their ids later, so nothing shifts
enum : uint8_t {
    ProjectKind = 1,
    SongKind = 2,
    TrackKind = 3,
    ClipKind = 4,
    CurveKind = 5,
    MarkerKind = 6,
    SendKind = 7,
    AudioSourceKind = 8,
    WorkspaceKind = 9,
    RulerKind = 10,
    PluginChainKind = 11,
    PluginKind = 12,
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

enum : uint8_t {
    ProjectSubtype = 1,
};

// A fade curve belongs one to one to its clip: its id is derived from the
// clip's number. Gain curves (derived, [5|1|owner]) and automation curves
// (several per owner: counter ids) get their ids with T108. Subtype 5 is
// unused: the bounce track has no gain curve
enum : uint8_t {
    GainCurveSubtype = 1,
    FadeInCurveSubtype = 2,
    FadeOutCurveSubtype = 3,
    AutomationCurveSubtype = 4,
};

enum : uint8_t {
    MarkerSubtype = 1,
};

enum : uint8_t {
    SendSubtype = 1,
};

enum : uint8_t {
    AudioSourceSubtype = 1,
};

enum : uint8_t {
    WorkspaceSubtype = 1,
};

enum : uint8_t {
    RulerSubtype = 1,
};

enum : uint8_t {
    Lv2PluginSubtype = 1,
    Vst3PluginSubtype = 2,
    ClapPluginSubtype = 3,
    InternalPluginSubtype = 4,
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
    static constexpr bool FromOwner = false;
    static constexpr uint8_t Kind = TrackKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(AudioTrackSubtype);
    static constexpr const char* Name = "audiotrack";
    using Object = TAudioTrack;
};

struct BusTrackTag
{
    static constexpr bool FromOwner = false;
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

// Clips: ClipId > AudioClipId

struct ClipTag
{
    static constexpr uint8_t Kind = ClipKind;
    static constexpr uint64_t Subtypes = TObjectIds::AllSubtypes;
    static constexpr const char* Name = "clip";
    using Object = TClip;
};

struct AudioClipTag
{
    static constexpr bool FromOwner = false;
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
// Songs: one subtype. In the code a song is still a TSheet

struct SongTag
{
    static constexpr bool FromOwner = false;
    static constexpr uint8_t Kind = SongKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(SongSubtype);
    static constexpr const char* Name = "song";
    using Object = TSheet;
};

// The project: one fixed id, on number 0 (TObjectId::root()), the owner of the
// project master. Not saved: it is the same in every project

struct ProjectTag
{
    static constexpr bool FromOwner = false;
    static constexpr uint8_t Kind = ProjectKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(ProjectSubtype);
    static constexpr const char* Name = "project";
    using Object = TProject;
};

using ProjectId = TObjectId<ProjectTag>;

// A song's ruler: one to one with its song, its id derived from the song's (the
// project's own ruler on number 0). Not in the file: it follows from its song

struct RulerTag
{
    static constexpr bool FromOwner = true;
    static constexpr uint8_t Kind = RulerKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(RulerSubtype);
    static constexpr const char* Name = "ruler";
    using Object = TTimeLineRuler;
};

struct MarkerTag
{
    static constexpr bool FromOwner = false;
    static constexpr uint8_t Kind = MarkerKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(MarkerSubtype);
    static constexpr const char* Name = "marker";
    using Object = TTimeLineMarker;
};

struct SendTag
{
    static constexpr bool FromOwner = false;
    static constexpr uint8_t Kind = SendKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(SendSubtype);
    static constexpr const char* Name = "send";
    using Object = TSend;
};

// An audio source: one per audio file of the project, the stream the resources
// manager keeps. The streams it hands out as copies carry the same id and are
// not in the pool

struct AudioSourceTag
{
    static constexpr bool FromOwner = false;
    static constexpr uint8_t Kind = AudioSourceKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(AudioSourceSubtype);
    static constexpr const char* Name = "audiosource";
    using Object = TBufferedAudioStreamReader;
};

// A workspace: a session of a song or of the project that shows some of its
// tracks

struct WorkspaceTag
{
    static constexpr bool FromOwner = false;
    static constexpr uint8_t Kind = WorkspaceKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(WorkspaceSubtype);
    static constexpr const char* Name = "workspace";
    using Object = TWorkspace;
};

// Curves: TCurve (gain, automation) and TFadeCurve have no common base below
// TContextItem, so the tag over all curves names that (Remon 2026-11-11). Not
// downcast after find(CurveId): the exact tags name the concrete type
struct CurveTag
{
    static constexpr uint8_t Kind = CurveKind;
    static constexpr uint64_t Subtypes = TObjectIds::AllSubtypes;
    static constexpr const char* Name = "curve";
    using Object = TContextItem;
};

struct FadeInCurveTag
{
    static constexpr bool FromOwner = true;
    static constexpr uint8_t Kind = CurveKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(FadeInCurveSubtype);
    static constexpr const char* Name = "fadein";
    using Object = TFadeCurve;
};

struct FadeOutCurveTag
{
    static constexpr bool FromOwner = true;
    static constexpr uint8_t Kind = CurveKind;
    static constexpr uint64_t Subtypes = TObjectIds::subtype(FadeOutCurveSubtype);
    static constexpr const char* Name = "fadeout";
    using Object = TFadeCurve;
};

using CurveId = TObjectId<CurveTag>;
using RulerId = TObjectId<RulerTag>;
using MarkerId = TObjectId<MarkerTag>;
using SendId = TObjectId<SendTag>;
using ClipId = TObjectId<ClipTag>;
using AudioClipId = TObjectId<AudioClipTag>;
using SongId = TObjectId<SongTag>;
using AudioSourceId = TObjectId<AudioSourceTag>;
using WorkspaceId = TObjectId<WorkspaceTag>;

// At start-up, before anything logs an id. Only exact tags: ids are made with
// those, so they are the ones the log shows
inline void register_object_id_names()
{
    TObjectIds::register_tag(TObjectIds::make_tag(TrackKind, AudioTrackSubtype), AudioTrackTag::Name, AudioTrackTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(TrackKind, BusTrackSubtype), BusTrackTag::Name, BusTrackTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(TrackKind, SongMasterSubtype), SongMasterTag::Name, SongMasterTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(TrackKind, ProjectMasterSubtype), ProjectMasterTag::Name, ProjectMasterTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(TrackKind, BounceTrackSubtype), BounceTrackTag::Name, BounceTrackTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(ClipKind, AudioClipSubtype), AudioClipTag::Name, AudioClipTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(SongKind, SongSubtype), SongTag::Name, SongTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(ProjectKind, ProjectSubtype), ProjectTag::Name, ProjectTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(RulerKind, RulerSubtype), RulerTag::Name, RulerTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(MarkerKind, MarkerSubtype), MarkerTag::Name, MarkerTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(SendKind, SendSubtype), SendTag::Name, SendTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(AudioSourceKind, AudioSourceSubtype), AudioSourceTag::Name, AudioSourceTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(WorkspaceKind, WorkspaceSubtype), WorkspaceTag::Name, WorkspaceTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(CurveKind, FadeInCurveSubtype), FadeInCurveTag::Name, FadeInCurveTag::FromOwner);
    TObjectIds::register_tag(TObjectIds::make_tag(CurveKind, FadeOutCurveSubtype), FadeOutCurveTag::Name, FadeOutCurveTag::FromOwner);
    // A clip id travels as item data in a drag from the resources window: the
    // stream of a QVariant names its type, which the reading side must know
    qRegisterMetaType<ClipId>();
    // Ids in the item data of lists and menus (QVariant)
    qRegisterMetaType<SongId>();
    qRegisterMetaType<TrackId>();
    qRegisterMetaType<SendId>();
    qRegisterMetaType<MarkerId>();
    qRegisterMetaType<AudioSourceId>();
}
