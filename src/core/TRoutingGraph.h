// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TObjectIdTags.h"

#include <QList>
#include <QSet>

// The routing as a graph: a node is a track, an edge a send to a track. A
// send to a port (a hardware or software bus) sends nowhere further and is no
// edge. Feedback loops are excluded (T64): a send is refused when it would
// close one.
class TRoutingGraph
{
public:
    // True when an edge from -> to closes a loop: to is from itself, or from
    // is reachable from to. targetsOf(id) gives the ids an id sends to.
    template <typename TargetsOf>
    static bool closes_loop(TrackId from, TrackId to, TargetsOf targetsOf)
    {
        QList<TrackId> pending{to};
        QSet<TrackId> visited;
        while (!pending.isEmpty()) {
            const TrackId id = pending.takeLast();
            if (id == from) {
                return true;
            }
            if (visited.contains(id)) {
                continue;
            }
            visited.insert(id);
            pending.append(targetsOf(id));
        }
        return false;
    }
};
