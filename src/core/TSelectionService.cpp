/*
    Copyright (C) 2005 - 2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TSelectionService.h"

#include <QVariant>
#include "TClip.h"
#include "TSelectionTimeBounds.h"
#include <QObject>

TSelectionService::TSelectionService(QObject* parent)
    : QObject(parent)
{
}

void TSelectionService::select(TTimeLineEntity* entity)
{
    Q_ASSERT(entity);

    auto* clip = dynamic_cast<TClip*>(entity);
    Q_ASSERT(clip);

    const ClipId id = clip->object_id();
    if (!m_registry.contains(id)) {
        m_registry.insert(id, entity);

        if (auto* qobj = dynamic_cast<QObject*>(entity)) {
            qobj->setProperty("selected", true);
        }

        emit selectionChanged();
    }
}

void TSelectionService::deselect(TTimeLineEntity* entity)
{
    Q_ASSERT(entity);

    auto* clip = dynamic_cast<TClip*>(entity);
    Q_ASSERT(clip);

    const ClipId id = clip->object_id();
    if (m_registry.remove(id) > 0) {
        if (auto* qobj = dynamic_cast<QObject*>(entity)) {
            qobj->setProperty("selected", false);
        }
        emit selectionChanged();
    }
}

void TSelectionService::clear()
{
    if (m_registry.isEmpty()) return;

    for (TTimeLineEntity* entity : m_registry) {
        if (auto* qobj = dynamic_cast<QObject*>(entity)) {
            qobj->setProperty("selected", false);
        }
    }

    m_registry.clear();
    emit selectionChanged();
}

TSelectionTimeBounds TSelectionService::get_selection_time_bounds() const
{
    TSelectionTimeBounds bounds;
    if (m_registry.isEmpty()) {
        return bounds;
    }

    for (TTimeLineEntity* entity : m_registry) {
        if (entity->get_location_start() < bounds.start) {
            bounds.start = entity->get_location_start();
        }
        if (entity->get_location_end() > bounds.end) {
            bounds.end = entity->get_location_end();
        }
    }

    return bounds;
}
