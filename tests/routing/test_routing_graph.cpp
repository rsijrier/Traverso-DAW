// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TRoutingGraph::closes_loop() (T64): a send to the own
// bus, a loop through two and through three bus tracks are refused; a send
// to a track that sends nowhere, a send next to an existing one and a diamond
// (two paths to one bus, no loop) are allowed; a loop that already exists
// further on does not hang the check.

#include "TObjectIdTestAccess.h"
#include "TRoutingGraph.h"

#include <QHash>
#include <cstdio>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

int main()
{
    // Numbers: 1-4 bus tracks, 10 an audio track, 100 a track that sends
    // nowhere
    auto bus = [](uint64_t number) {
        const uint64_t value = (uint64_t(TObjectIds::make_tag(TrackKind, BusTrackSubtype)) << TObjectIds::SequenceBits) | number;
        return TrackId(*TObjectIdTestAccess::from_value<BusTrackId>(value));
    };
    auto audio = [](uint64_t number) {
        const uint64_t value = (uint64_t(TObjectIds::make_tag(TrackKind, AudioTrackSubtype)) << TObjectIds::SequenceBits) | number;
        return TrackId(*TObjectIdTestAccess::from_value<AudioTrackId>(value));
    };
    const TrackId b1 = bus(1), b2 = bus(2), b3 = bus(3), b4 = bus(4), a10 = audio(10), out = bus(100);

    QHash<TrackId, QList<TrackId>> sends;
    auto targetsOf = [&sends](TrackId id) { return sends.value(id); };

    check(TRoutingGraph::closes_loop(b1, b1, targetsOf), "a send to the own bus closes a loop");
    check(!TRoutingGraph::closes_loop(b1, out, targetsOf), "a send to a track that sends nowhere does not");

    sends[b1] = {b2};
    check(TRoutingGraph::closes_loop(b2, b1, targetsOf), "a send back to the bus that sends here closes a loop");
    check(!TRoutingGraph::closes_loop(a10, b1, targetsOf), "an audio track to a bus does not");

    sends[b2] = {b3};
    check(TRoutingGraph::closes_loop(b3, b1, targetsOf), "a loop through three bus tracks is found");
    check(!TRoutingGraph::closes_loop(b1, b3, targetsOf), "a second path to a bus already reached is no loop");

    sends[a10] = {b1, b4};
    sends[b4] = {b3};
    check(!TRoutingGraph::closes_loop(b3, out, targetsOf), "a diamond into one bus and on to the out is no loop");
    // The graph alone: in the application nothing can send to an audio
    // track (its process bus is the sheet's render bus, never a target)
    check(TRoutingGraph::closes_loop(b3, a10, targetsOf), "graph only: an edge back to the source at the top closes a loop");

    // A loop that got in anyway (2 and 3 send to each other) must not hang
    // a check that passes through it
    sends[b3] = {b2};
    check(!TRoutingGraph::closes_loop(b1, b2, targetsOf), "a check through an existing loop ends, no loop back to the start");
    check(TRoutingGraph::closes_loop(b2, b3, targetsOf), "a send inside the existing loop is found");

    std::printf("%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
