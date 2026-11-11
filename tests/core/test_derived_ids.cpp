// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of the derived ids: every kind of derived object (song master,
// project master, bounce track, ruler, fade in, fade out) with the real kind
// and subtype numbers of TObjectIdTags.h, derived from its owner as core does
// it, next to owners with counter numbers, all in one pool: no id twice. A
// future derived owner that shares a number in the same role shows here.

#include "TObjectFactory.h"
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

// Stand-ins for the domain classes: one per kind, made under several tags
template <typename Set>
class Thing : public QObject
{
public:
    using IdTag = void;
    TObjectId<Set> object_id() const { return m_objectId; }

private:
    friend class TObjectFactory;
    TObjectId<Set> m_objectId;
};

template <uint8_t K, uint64_t S, bool Derived, typename Set>
struct T
{
    static constexpr bool FromOwner = Derived;
    static constexpr uint8_t Kind = K;
    static constexpr uint64_t Subtypes = S;
    static constexpr const char* Name = "test";
    using Object = Thing<Set>;
};

template <uint8_t K>
struct Set
{
    static constexpr uint8_t Kind = K;
    static constexpr uint64_t Subtypes = TObjectIds::AllSubtypes;
    static constexpr const char* Name = "set";
    using Object = Thing<Set>;
};

using TrackSet = Set<TrackKind>;
using CurveSet = Set<CurveKind>;
using SongSet = Set<SongKind>;
using ClipSet = Set<ClipKind>;
using RulerSet = Set<RulerKind>;

using Song = T<SongKind, TObjectIds::subtype(SongSubtype), false, SongSet>;
using Clip = T<ClipKind, TObjectIds::subtype(AudioClipSubtype), false, ClipSet>;
using AudioTrack = T<TrackKind, TObjectIds::subtype(AudioTrackSubtype), false, TrackSet>;
using BusTrack = T<TrackKind, TObjectIds::subtype(BusTrackSubtype), false, TrackSet>;
using SongMaster = T<TrackKind, TObjectIds::subtype(SongMasterSubtype), true, TrackSet>;
using ProjectMaster = T<TrackKind, TObjectIds::subtype(ProjectMasterSubtype), true, TrackSet>;
using BounceTrack = T<TrackKind, TObjectIds::subtype(BounceTrackSubtype), true, TrackSet>;
using Ruler = T<RulerKind, TObjectIds::subtype(RulerSubtype), true, RulerSet>;
using FadeIn = T<CurveKind, TObjectIds::subtype(FadeInCurveSubtype), true, CurveSet>;
using FadeOut = T<CurveKind, TObjectIds::subtype(FadeOutCurveSubtype), true, CurveSet>;

int main(int argc, char** argv)
{
    // The pool and the sequence assert they run on the main thread
    QCoreApplication app(argc, argv);

    TObjectIdGenerator ids;
    TObjectPool pool;
    TObjectFactory objects(ids, pool);
    std::set<std::string> seen;
    int made = 0;
    auto note = [&](const auto* object) {
        seen.insert(object->object_id().to_hex());
        ++made;
        return object;
    };

    // Two songs, each with its master, bounce track and ruler on the song's
    // number, tracks and clips with counter numbers and each clip's fades
    for (int song = 0; song < 2; ++song) {
        auto* s = note(objects.make<Thing<SongSet>, Song>());
        note(objects.make_owned<Thing<TrackSet>, SongMaster>(s->object_id()));
        note(objects.make_owned<Thing<TrackSet>, BounceTrack>(s->object_id()));
        note(objects.make_owned<Thing<RulerSet>, Ruler>(s->object_id()));
        for (int i = 0; i < 2; ++i) {
            note(objects.make<Thing<TrackSet>, AudioTrack>());
            note(objects.make<Thing<TrackSet>, BusTrack>());
            auto* clip = note(objects.make<Thing<ClipSet>, Clip>());
            note(objects.make_owned<Thing<CurveSet>, FadeIn>(clip->object_id()));
            note(objects.make_owned<Thing<CurveSet>, FadeOut>(clip->object_id()));
        }
    }
    // The project's own: its master and ruler on number 0
    auto* projectMaster = note(objects.make_owned<Thing<TrackSet>, ProjectMaster>(ProjectId::root()));
    note(objects.make_owned<Thing<RulerSet>, Ruler>(ProjectId::root()));

    check(int(seen.size()) == made, "every object, counter or derived, has an id of its own");
    check(projectMaster->object_id().is_valid() && projectMaster->object_id().sequence() == 0,
          "the project master, on number 0, is a valid id");
    check(projectMaster->object_id().to_hex() == "0304000000000000", "the project master is [3|4|0]");

    return failures == 0 ? 0 : 1;
}
