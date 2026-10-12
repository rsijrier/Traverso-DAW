/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TSplitEntityController.h"
#include "TProject.h"
#include "TProjectManager.h"
#include "TSheet.h"
#include "TSnapList.h"
#include "TTimeLineEntity.h"
#include "TSplitableTimeLineEntity.h"
#include "TContextPointer.h"
#include "TViewPortInterface.h"
#include "TSelectionTimeBounds.h"
#include "TSelectionService.h"
#include "Debugger.h"
#include <QUndoCommand>

TSplitEntityController::TSplitEntityController(TTimeLineEntity* entity, TSheet* sheet)
    : TCommandController(dynamic_cast<TContextItem*>(entity))
    , m_entity(entity)
    , m_sheet(sheet)
{
    Q_ASSERT(m_entity != nullptr);
    Q_ASSERT(m_sheet != nullptr);

    auto* splitable = dynamic_cast<TSplitableTimeLineEntity*>(m_entity);
    Q_ASSERT_X(splitable != nullptr, "TSplitEntityController", "Initiating split on a non-splitable entity!");
}

int TSplitEntityController::begin_hold()
{
    m_isHolding = true;
    emit isHoldingChanged();
    return 1;
}

int TSplitEntityController::finish_hold()
{
    m_isHolding = false;
    emit isHoldingChanged();
    return 1;
}

TSelectionTimeBounds TSplitEntityController::calculate_selection_bounds(const QList<TTimeLineEntity*>& selectedEntities) const
{
    TSelectionTimeBounds bounds;

    for (auto& entity : selectedEntities) {
        if (entity && dynamic_cast<TSplitableTimeLineEntity*>(entity)) {
            if (entity->get_location_start() < bounds.start) {
                bounds.start = entity->get_location_start();
            }
            if (entity->get_location_end() > bounds.end) {
                bounds.end = entity->get_location_end();
            }
        }
    }

    if (!bounds.isValid()) {
        bounds.start = m_entity->get_location_start();
        bounds.end = m_entity->get_location_end();
    }

    return bounds;
}

int TSplitEntityController::jog()
{
    m_splitLocation = cpointer().timeref_location();

    QList<TTimeLineEntity*> selectedEntities = m_sheet->get_selection_service()->get_selected_entities();

    TSelectionTimeBounds bounds = calculate_selection_bounds(selectedEntities);

    TTimeRef minValidSplit = bounds.start + TTimeRef(qint64(1));
    TTimeRef maxValidSplit = bounds.end - TTimeRef(qint64(1));

    if (m_splitLocation < minValidSplit) m_splitLocation = minValidSplit;
    if (m_splitLocation > maxValidSplit) m_splitLocation = maxValidSplit;

    if (m_sheet->is_snap_on()) {
        TSnapList::SnapFilters activeFilters = TSnapList::SnapAll;
        TViewPortInterface* viewport = cpointer().get_viewport();
        qreal scaleFactor = viewport ? viewport->get_timeref_scale_factor() : 1;

        TRichSnapResult snap = m_sheet->get_snap_list()->get_snap_context(m_splitLocation, scaleFactor, activeFilters);

        if (snap.snappedLocation >= minValidSplit && snap.snappedLocation <= maxValidSplit) {
            m_splitLocation = snap.snappedLocation;
        }

        if (snap.didSnap && !snap.snappedToTargets.isEmpty()) {
            emit snapTargetsTriggered(snap.snappedToTargets, m_splitLocation);
        }
    }

    emit splitLocationChanged();
    cpointer().set_canvas_cursor_data(QVariant::fromValue(TTimeRef::timeref_to_ms_2(m_splitLocation)));

    return 1;
}

void TSplitEntityController::do_keyboard_move(const TTimeRef& location)
{
    m_splitLocation = location;

    QList<TTimeLineEntity*> selectedEntities = m_sheet->get_selection_service()->get_selected_entities();

    TSelectionTimeBounds bounds = calculate_selection_bounds(selectedEntities);

    if (m_splitLocation < bounds.start) m_splitLocation = bounds.start;
    if (m_splitLocation > bounds.end)   m_splitLocation = bounds.end;

    emit splitLocationChanged();
    cpointer().set_canvas_cursor_location(m_splitLocation, cpointer().scene_y());
}


void TSplitEntityController::cancel_action()
{
    PENTER;
    m_isHolding = false;
    emit isHoldingChanged();

    m_splitLocation = TTimeRef();
    emit splitLocationChanged();

    cpointer().set_canvas_cursor_location(
        cpointer().on_first_input_event_timeref_location(),
        cpointer().scene_y()
    );
}

int TSplitEntityController::prepare_actions()
{
    PENTER;

    if (m_splitLocation == TTimeRef()) {
        TViewPortInterface* viewport = cpointer().get_viewport();
        qint64 scaleFactorFramesPerPixel = viewport ? viewport->get_timeref_scale_factor() : TTimeRef::UNIVERSAL_SAMPLE_RATE;
        m_splitLocation = static_cast<qint64>(cpointer().scene_x()) * scaleFactorFramesPerPixel;
    }

    auto* splitable = dynamic_cast<TSplitableTimeLineEntity*>(m_entity);
    if (!splitable || !splitable->validate_split_boundary(m_splitLocation)) {
        return -1;
    }

    return 1;
}

QUndoCommand* TSplitEntityController::get_undo_command()
{
    QList<TTimeLineEntity*> targets = m_sheet->get_selection_service()->get_selected_entities();
    QList<TSplitableTimeLineEntity*> splitableTargets;

    for (auto& entity : targets) {
        if (auto splitable = dynamic_cast<TSplitableTimeLineEntity*>(entity)) {
            splitableTargets.append(splitable);
        }
    }

    if (splitableTargets.isEmpty()) {
        if (auto splitable = dynamic_cast<TSplitableTimeLineEntity*>(m_entity)) {
            splitableTargets.append(splitable);
        }
    }

    return m_sheet->create_command_split_entities(splitableTargets, m_splitLocation, tr("Split Selection"));
}

const TSelectionTimeBounds TSplitEntityController::get_time_line_entity_constraints()
{
    QList<TTimeLineEntity*> selectedEntities = m_sheet->get_selection_service()->get_selected_entities();

    TSelectionTimeBounds bounds = calculate_selection_bounds(selectedEntities);

    return bounds;
}

void TSplitEntityController::move_left()
{
    if (m_sheet->is_snap_on()) return prev_snap_pos();

    TViewPortInterface* viewport = cpointer().get_viewport();
    if (!viewport) return;

    qint64 scaleFactorFramesPerPixel = viewport->get_timeref_scale_factor();
    qint64 navigationSpeed = pm().get_project()->get_keyboard_arrow_key_navigation_speed();

    qint64 frameDelta = navigationSpeed * 5 * scaleFactorFramesPerPixel;
    do_keyboard_move(m_splitLocation - TTimeRef(frameDelta));
}

void TSplitEntityController::move_right()
{
    if (m_sheet->is_snap_on()) return next_snap_pos();

    TViewPortInterface* viewport = cpointer().get_viewport();
    if (!viewport) return;

    qint64 scaleFactorFramesPerPixel = viewport->get_timeref_scale_factor();
    qint64 navigationSpeed = pm().get_project()->get_keyboard_arrow_key_navigation_speed();

    qint64 frameDelta = navigationSpeed * 5 * scaleFactorFramesPerPixel;
    do_keyboard_move(m_splitLocation + TTimeRef(frameDelta));
}

void TSplitEntityController::next_snap_pos()
{
    do_keyboard_move(m_sheet->get_snap_list()->next_snap_pos(m_splitLocation));
}

void TSplitEntityController::prev_snap_pos()
{
    do_keyboard_move(m_sheet->get_snap_list()->prev_snap_pos(m_splitLocation));
}

