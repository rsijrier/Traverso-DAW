// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the derived ids: every kind of derived object (song master,
// project master, bounce track, ruler, fade in, fade out) with the real kind
// and subtype numbers of TObjectIdTags.h, derived from its owner as core does
// it, next to owners with counter numbers: no id twice. The derived peers
// (masters, bounce track) go into the pool as their makers do it; the parts
// (ruler, fades) are not in the pool. A future derived owner that shares a
// number in the same role shows here.

#include "TObjectFactory.h"
#include "TObjectIdTestAccess.h"
#include "TObjectIdTags.h"

#include <QCoreApplication>

#include <cstdio>
#include <set>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

// Stand-ins for the domain classes. An owner: a counter id, made by the
// factory with its id first
template <typename Tag>
class Owner : public QObject
{
public:
    using IdTag = Tag;
    explicit Owner(TObjectId<Tag> id) : m_objectId(id) {}
    TObjectId<Tag> object_id() const { return m_objectId; }

private:
    TObjectId<Tag> m_objectId;
};

// A derived object of one kind, made under several tags
template <typename Set>
class Derived : public QObject
{
public:
    using IdTag = void;
    Derived(TObjectId<Set> id, QObject*) : m_objectId(id) {}
    TObjectId<Set> object_id() const { return m_objectId; }

private:
    TObjectId<Set> m_objectId;
};

template <uint8_t K>
struct Set
{
    static constexpr bool InPool = true;
    static constexpr uint8_t Kind = K;
    static constexpr uint64_t Subtypes = TObjectIds::AllSubtypes;
    static constexpr const char* Name = "set";
    using Object = Derived<Set>;
};

using TrackSet = Set<TrackKind>;
using CurveSet = Set<CurveKind>;
using RulerSet = Set<RulerKind>;

template <uint8_t K, uint64_t S, typename Object_>
struct Counter
{
    static constexpr bool InPool = true;
    static constexpr bool FromOwner = false;
    static constexpr uint8_t Kind = K;
    static constexpr uint64_t Subtypes = S;
    static constexpr const char* Name = "counter";
    using Object = Object_;
};

template <uint8_t K, uint64_t S, bool Peer, typename Set_>
struct DerivedTag
{
    static constexpr bool InPool = Peer;
    static constexpr bool FromOwner = true;
    static constexpr uint8_t Kind = K;
    static constexpr uint64_t Subtypes = S;
    static constexpr const char* Name = "derived";
    using Object = Derived<Set_>;
};

struct SongTag_;
struct ClipTag_;
struct TrackTag_;
using Song = Counter<SongKind, TObjectIds::subtype(SongSubtype), Owner<SongTag_>>;
struct SongTag_ : Song {};
using Clip = Counter<ClipKind, TObjectIds::subtype(AudioClipSubtype), Owner<ClipTag_>>;
struct ClipTag_ : Clip {};
using AudioTrack = Counter<TrackKind, TObjectIds::subtype(AudioTrackSubtype), Owner<TrackTag_>>;
struct TrackTag_ : AudioTrack {};
struct BusTrackTag_;
using BusTrack = Counter<TrackKind, TObjectIds::subtype(BusTrackSubtype), Owner<BusTrackTag_>>;
struct BusTrackTag_ : BusTrack {};
struct ProjectTag_ : Counter<ProjectKind, TObjectIds::subtype(ProjectSubtype), QObject> {};

// The peers (masters, bounce track) and the parts (ruler, fades) of the real
// kind and subtype numbers
using SongMaster = DerivedTag<TrackKind, TObjectIds::subtype(SongMasterSubtype), true, TrackSet>;
using ProjectMaster = DerivedTag<TrackKind, TObjectIds::subtype(ProjectMasterSubtype), true, TrackSet>;
using BounceTrack = DerivedTag<TrackKind, TObjectIds::subtype(BounceTrackSubtype), true, TrackSet>;
using Ruler = DerivedTag<RulerKind, TObjectIds::subtype(RulerSubtype), false, RulerSet>;
using FadeIn = DerivedTag<CurveKind, TObjectIds::subtype(FadeInCurveSubtype), false, CurveSet>;
using FadeOut = DerivedTag<CurveKind, TObjectIds::subtype(FadeOutCurveSubtype), false, CurveSet>;

// The project: no number of its own
class Project : public QObject
{
public:
    using IdTag = ProjectTag_;
    TObjectId<ProjectTag_> object_id() const { return TObjectId<ProjectTag_>::root(); }
};

int main(int argc, char** argv)
{
    // The pool and the sequence assert they run on the main thread
    QCoreApplication app(argc, argv);

    TObjectIdGenerator ids;
    TObjectPool pool;
    TObjectFactory objects(ids, pool);
    std::set<std::string> seen;
    int made = 0;
    auto note = [&](auto* object) {
        seen.insert(object->object_id().to_hex());
        ++made;
        return object;
    };
    // A derived peer goes into the pool, as its maker does it: a derived id
    // made twice is refused there
    bool peersIn = true;
    auto enroll = [&](auto tag, typename decltype(tag)::Object* object) {
        peersIn = pool.add<decltype(tag)>(object) && peersIn;
        return object;
    };

    // Two songs, each with its master, bounce track and ruler on the song's
    // number, tracks and clips with counter numbers and each clip's fades
    for (int song = 0; song < 2; ++song) {
        auto* s = note(objects.make<Owner<SongTag_>, SongTag_>());
        note(enroll(SongMaster{}, TObjectFactory::make_derived<SongMaster>(s)));
        note(enroll(BounceTrack{}, TObjectFactory::make_derived<BounceTrack>(s)));
        note(TObjectFactory::make_derived<Ruler>(s));
        for (int i = 0; i < 2; ++i) {
            note(objects.make<Owner<TrackTag_>, TrackTag_>());
            note(objects.make<Owner<BusTrackTag_>, BusTrackTag_>());
            auto* clip = note(objects.make<Owner<ClipTag_>, ClipTag_>());
            note(TObjectFactory::make_derived<FadeIn>(clip));
            note(TObjectFactory::make_derived<FadeOut>(clip));
        }
    }
    // The project's own: its master and ruler on number 0
    Project project;
    auto* projectMaster = note(enroll(ProjectMaster{}, TObjectFactory::make_derived<ProjectMaster>(&project)));
    note(TObjectFactory::make_derived<Ruler>(&project));

    check(int(seen.size()) == made, "every object, counter or derived, peer or part, has an id of its own");
    check(peersIn, "every derived peer goes into the pool: no derived id twice");
    check(pool.find_raw(TObjectIdTestAccess::value(projectMaster->object_id())) == projectMaster,
          "a derived peer is found by a bare id");
    check(projectMaster->object_id().is_valid() && projectMaster->object_id().sequence() == 0,
          "the project master, on number 0, is a valid id");
    check(projectMaster->object_id().to_hex() == "0304000000000000", "the project master is [3|4|0]");

    return failures == 0 ? 0 : 1;
}
