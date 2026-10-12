/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommandController.h"
#include "THoldInteraction.h"
#include "TTransactional.h"
#include "TSnapableTimeController.h"
#include "TShuttleableController.h"
#include "TTimeRef.h"

class TSheet;
class QUndoCommand;
class TTimeLineEntity;
struct TSelectionTimeBounds;

class TSplitEntityController final : public TCommandController,
                                     public THoldInteraction,
                                     public TTransactional,
                                     public TSnapableTimeController,
                                     public TShuttleableController
{
    Q_OBJECT
    Q_INTERFACES(THoldInteraction
                TTransactional
                TSnapableTimeController
                TShuttleableController)

    Q_PROPERTY(bool isHolding READ is_holding NOTIFY isHoldingChanged)
    Q_PROPERTY(qint64 splitFrame READ get_split_frame NOTIFY splitLocationChanged)

public:
    explicit TSplitEntityController(TTimeLineEntity* entity, TSheet* sheet);
    ~TSplitEntityController() override = default;

    // THoldInteraction contract
    int begin_hold() final;
    int jog() final;
    int finish_hold() final;
    void cancel_action() final;
    bool canvas_cursor_follows_mouse_cursor() const final { return true; }
    TraversoDAW::CursorType get_cursor_type() const final {return TraversoDAW::CursorType::Split;}

    // TTransactional contract
    int prepare_actions() final;
    QUndoCommand* get_undo_command() final;

    // TShuttleableController contract
    TViewPortInterface::ScrollModes scrolling_mode() const final { return TViewPortInterface::Horizontal; }
    qreal shuttle_range_x() const final { return 0.3; }
    const TSelectionTimeBounds get_time_line_entity_constraints() final;

    // TSnapableTimeController contract
    void move_left() final;
    void move_right() final;
    void next_snap_pos() final;
    void prev_snap_pos() final;

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
