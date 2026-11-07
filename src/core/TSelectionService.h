/*
    Copyright (C) 2005 - 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include <QObject>
#include <QList>
#include "TTimeLineEntity.h"
#include "TSelectionTimeBounds.h"

class TSelectionService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int selectionCount READ get_selection_count NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ has_selection NOTIFY selectionChanged)

public:
    explicit TSelectionService(QObject* parent = nullptr);
    ~TSelectionService() override = default;

    void select(TTimeLineEntity* entity);
    void deselect(TTimeLineEntity* entity);
    void clear();

    bool is_selected(const TTimeLineEntity* entity) const { return m_registry.contains(entity); }
    int get_selection_count() const { return m_registry.size(); }
    bool has_selection() const { return !m_registry.isEmpty(); }

    QList<TTimeLineEntity*> get_selected_entities() const { return m_registry; }
    TSelectionTimeBounds get_selection_time_bounds() const;

signals:
    void selectionChanged();

private:
    // In the order they were selected
    QList<TTimeLineEntity*> m_registry;
};
