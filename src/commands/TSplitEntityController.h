/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommandController.h"
#include "TTimeRef.h"

class TSheet;
class QUndoCommand;
class TTimeLineEntity;
struct TSelectionTimeBounds;

class TSplitEntityController final : public TCommandController
{
    Q_OBJECT

    Q_PROPERTY(bool isHolding READ is_holding NOTIFY isHoldingChanged)
    Q_PROPERTY(qint64 splitFrame READ get_split_frame NOTIFY splitLocationChanged)

public:
    explicit TSplitEntityController(TTimeLineEntity* entity, TSheet* sheet);
    ~TSplitEntityController() override = default;

    // TCommandController
    bool is_hold() const override { return true; }
    bool begin_hold() override;
    void update() override;
    void cancel() override;
    TCommitResult commit() override;
    TraversoDAW::CursorType cursor_type() const override { return TraversoDAW::CursorType::Split; }
    TViewPortInterface::ScrollModes scrolling_mode() const override { return TViewPortInterface::Horizontal; }
    TSelectionTimeBounds time_constraints() const override;

    // Keyboard navigation. Not reachable yet, it moves to the input layer.
    void move_left();
    void move_right();
    void next_snap_pos();
    void prev_snap_pos();

    bool is_holding() const { return m_isHolding; }
    qint64 get_split_frame() const { return m_splitLocation.universal_frame(); }
    TTimeRef get_split_location() const { return m_splitLocation; }

public slots:
    // void toggle_snap_on_off() { execute_snap_toggle(m_contextItem); }

signals:
    void isHoldingChanged();
    void splitLocationChanged();
    void snapTargetsTriggered(const QList<QObject*>& targets, TTimeRef snapFrame);

private:
    void do_keyboard_move(const TTimeRef& location);
    TSelectionTimeBounds calculate_selection_bounds(const QList<TTimeLineEntity*>& selectedEntities) const;

    TTimeLineEntity* m_entity{nullptr};
    TSheet*          m_sheet{nullptr};
    TTimeRef         m_splitLocation;
    bool             m_isHolding{false};
};
