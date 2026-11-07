/*
    Copyright (C) 2005 - 2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TSelectionService.h"

#include <QVariant>
#include "TSelectionTimeBounds.h"
#include <QObject>

TSelectionService::TSelectionService(QObject* parent)
    : QObject(parent)
{
}

void TSelectionService::select(TTimeLineEntity* entity)
{
    if (!entity) return;

    if (!m_registry.contains(entity)) {
        m_registry.append(entity);

        if (auto* qobj = dynamic_cast<QObject*>(entity)) {
            qobj->setProperty("selected", true);
        }

        emit selectionChanged();
    }
}

void TSelectionService::deselect(TTimeLineEntity* entity)
{
    if (!entity) return;

    if (m_registry.removeAll(entity) > 0) {
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
