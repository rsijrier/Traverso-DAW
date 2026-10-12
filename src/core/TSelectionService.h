/*
    Copyright (C) 2005 - 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include "TTimeLineEntity.h"
#include "TSelectionTimeBounds.h"

class TSelectionService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int selectionCount READ get_selection_count NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ has_selection NOTIFY selectionChanged)

public:
    enum SelectionContext {
        ContextAll,
        ContextTrackLane,
        ContextAutomation
    };
    Q_ENUM(SelectionContext)

    explicit TSelectionService(QObject* parent = nullptr);
    ~TSelectionService() override = default;

    void select_with_context(TTimeLineEntity* entity, SelectionContext activeContext);
    void deselect(TTimeLineEntity* entity);
    void clear();

    bool is_selected(qint64 id) const { return m_registry.contains(id); }
    int get_selection_count() const { return m_registry.size(); }
    bool has_selection() const { return !m_registry.isEmpty(); }

    QList<TTimeLineEntity*> get_selected_entities() const { return m_registry.values(); }
    TSelectionTimeBounds get_selection_time_bounds() const;

signals:
    void selectionChanged();

private:
    QHash<qint64, TTimeLineEntity*> m_registry;
};
