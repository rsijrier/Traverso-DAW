/*
Copyright (C) 2006-2024 Nicola Doebelin, Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#include "TSnapList.h"
#include "TTimeLineEntity.h"
#include "TLocation.h"
#include "TSession.h"
#include "TSheet.h"
#include "TConfig.h"
#include "Debugger.h"
#include <algorithm>
#include <cmath>

TSnapList::TSnapList(TSession* session)
    : m_session(session)
{
    Q_ASSERT(m_session != nullptr);
}

void TSnapList::mark_dirty()
{
    m_isDirty = true;
    m_wasDirty = true;
}

bool TSnapList::was_dirty()
{
    bool ret = m_wasDirty;
    m_wasDirty = false;
    return ret;
}

void TSnapList::update_snaplist_algorithm()
{
    PENTER;
    auto startTime = TTimeRef::get_nanoseconds_since_epoch();

    m_uniqueSnapPositions.clear();
    m_locationRegistry.clear();

    TSheet* sheet = qobject_cast<TSheet*>(m_session);
    if (!sheet) return;

    QList<TTimeLineEntity*> allEntities = sheet->get_all_active_timeline_entities();

    m_uniqueSnapPositions.reserve(allEntities.size() * 2);
    m_locationRegistry.reserve(allEntities.size() * 2);

    for (TTimeLineEntity* entity : allEntities) {
        if (!entity || !entity->get_location()) continue;

        TLocation* location = entity->get_location();
        if (!location->is_snappable() || location->is_moving()) continue;

        TTimeRef start = location->get_start();
        TTimeRef end = location->get_end();

        m_uniqueSnapPositions.append(start);

        if (auto* qobj = dynamic_cast<QObject*>(entity)) {
            m_locationRegistry.insert(start.universal_frame(), qobj);

            if (end != start && end != TTimeRef()) {
                m_uniqueSnapPositions.append(end);
                m_locationRegistry.insert(end.universal_frame(), qobj);
            }
        }
    }

    std::sort(m_uniqueSnapPositions.begin(), m_uniqueSnapPositions.end());
    m_uniqueSnapPositions.erase(
        std::unique(m_uniqueSnapPositions.begin(), m_uniqueSnapPositions.end()),
        m_uniqueSnapPositions.end()
        );

    m_isDirty = false;

    auto totaltime = TTimeRef::get_nanoseconds_since_epoch() - startTime;
    printf("SnapList::update_snaplist_algorithm completed in: %ld ns\n", totaltime);
}

TRichSnapResult TSnapList::get_snap_context(const TTimeRef& mouseLocation, qint64 scaleFactor, SnapFilters activeFilters)
{
    if (m_isDirty) {
        update_snaplist_algorithm();
    }

    TRichSnapResult result;
    result.snappedLocation = mouseLocation;
    result.didSnap = false;

    if (activeFilters == NoSnap || m_uniqueSnapPositions.isEmpty()) {
        return result;
    }

    auto it = std::lower_bound(m_uniqueSnapPositions.begin(), m_uniqueSnapPositions.end(), mouseLocation);

    TTimeRef closestMatch;
    if (it == m_uniqueSnapPositions.end()) {
        closestMatch = m_uniqueSnapPositions.last();
    } else if (it == m_uniqueSnapPositions.begin()) {
        closestMatch = m_uniqueSnapPositions.first();
    } else {
        TTimeRef next = *it;
        TTimeRef prev = *(it - 1);
        closestMatch = (next - mouseLocation < mouseLocation - prev) ? next : prev;
    }

    int snapRangePixels = config().get_property("Snap", "range", 10).toInt();
    qint64 maxFrameDistance = static_cast<qint64>(snapRangePixels) * scaleFactor;
    qint64 actualDistanceFrames = std::abs(closestMatch.universal_frame() - mouseLocation.universal_frame());

    if (actualDistanceFrames <= maxFrameDistance) {
        QList<QPointer<QObject>> potentialTargets = m_locationRegistry.values(closestMatch.universal_frame());
        QList<QObject*> validatedTargets;

        for (auto &target : potentialTargets) {
            if (target.isNull()) continue;

            if (auto* entity = dynamic_cast<TTimeLineEntity*>(target.data())) {

                if (entity->get_timeline_entity_type() & activeFilters) {
                    validatedTargets.append(target.data());
                }
            }
        }

        if (!validatedTargets.isEmpty()) {
            result.didSnap = true;
            result.snappedLocation = closestMatch;
            result.snappedToTargets = validatedTargets;
        }
    }

    return result;
}

TTimeRef TSnapList::get_snap_value(const TTimeRef& location)
{
    TRichSnapResult res = get_snap_context(location, 1, SnapAll);
    return res.snappedLocation;
}

TTimeRef TSnapList::next_snap_pos(const TTimeRef& location)
{
    if (m_isDirty) update_snaplist_algorithm();
    if (m_uniqueSnapPositions.isEmpty()) return location;

    auto it = std::upper_bound(m_uniqueSnapPositions.begin(), m_uniqueSnapPositions.end(), location);
    return (it != m_uniqueSnapPositions.end()) ? *it : location;
}

TTimeRef TSnapList::prev_snap_pos(const TTimeRef& location)
{
    if (m_isDirty) update_snaplist_algorithm();
    if (m_uniqueSnapPositions.isEmpty()) return location;

    auto it = std::lower_bound(m_uniqueSnapPositions.begin(), m_uniqueSnapPositions.end(), location);
    return (it != m_uniqueSnapPositions.begin()) ? *(it - 1) : TTimeRef();
}
