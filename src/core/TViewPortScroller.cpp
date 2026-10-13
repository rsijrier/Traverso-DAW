/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TViewPortScroller.h"
#include "TContextPointer.h"
#include "TShuttleableController.h"
#include "TSelectionTimeBounds.h"

#include <algorithm>

TViewportScroller::TViewportScroller(TViewPortInterface::ScrollModes modes)
    : m_scrollModes(modes)
{
    // Same curve as the legacy TMoveCommand shuttle
    m_shuttleCurve.setType(QEasingCurve::InOutQuad);
}

bool TViewportScroller::calculate_shuttle_intensity(TShuttleableController *shuttleable,
                                                    qreal &calculatedHorizontalShuttleVelocity,
                                                    qreal &calculatedVerticalShuttleVelocity)
{
    calculatedHorizontalShuttleVelocity = 0.0;
    calculatedVerticalShuttleVelocity = 0.0;

    TContextPointer& pointer = cpointer();
    TViewPortInterface* viewport = pointer.get_viewport();

    if (!viewport || !shuttleable) {
        return false;
    }

    if (m_scrollModes.testFlag(TViewPortInterface::Horizontal)) {
        calculatedHorizontalShuttleVelocity = determine_shuttle_vector(
            pointer.mouse_viewport_x(),
            viewport->viewport_width(),
            shuttleable->shuttle_range_x()
            );
    }

    if (m_scrollModes.testFlag(TViewPortInterface::Vertical)) {
        calculatedVerticalShuttleVelocity = determine_shuttle_vector(
            pointer.mouse_viewport_y(),
            viewport->viewport_height(),
            shuttleable->shuttle_range_y()
            );
    }

    // No horizontal shuttling once the controller's time range reached its limit
    if (!qFuzzyIsNull(calculatedHorizontalShuttleVelocity)
        && calculate_scroll_step(shuttleable, calculatedHorizontalShuttleVelocity, 0.0).x() == 0) {
        calculatedHorizontalShuttleVelocity = 0.0;
    }

    return !qFuzzyIsNull(calculatedHorizontalShuttleVelocity) || !qFuzzyIsNull(calculatedVerticalShuttleVelocity);
}

qreal TViewportScroller::determine_shuttle_vector(int mouseViewportPosition, int viewportDimension, qreal triggerZoneWidth)
{
    qreal safeZoneWidth = qMin(0.5, qMax(0.0, triggerZoneWidth));
    if (viewportDimension <= 0 || qFuzzyIsNull(safeZoneWidth)) {
        return 0.0;
    }

    qreal normalizedPosition = qreal(mouseViewportPosition) / viewportDimension;
    qreal accelerationProgress = 0.0;
    int movementDirection = 0;

    if (normalizedPosition < safeZoneWidth) {
        movementDirection = -1;
        accelerationProgress = (safeZoneWidth - normalizedPosition) / safeZoneWidth;
    }

    else if (normalizedPosition > (1.0 - safeZoneWidth)) {
        movementDirection = 1;
        accelerationProgress = (normalizedPosition - (1.0 - safeZoneWidth)) / safeZoneWidth;
    }

    if (movementDirection != 0) {
        qreal velocity = m_shuttleCurve.valueForProgress(qMin(accelerationProgress, 1.0));
        // Like the legacy TMoveCommand shuttle: faster when the mouse is beyond the viewport edge
        if (accelerationProgress > 1.0) {
            velocity *= 1.5;
        }
        return velocity * movementDirection;
    }

    return 0.0;
}

QPoint TViewportScroller::calculate_scroll_step(TShuttleableController* shuttleable,
                                               qreal horizontalShuttleVelocity,
                                               qreal verticalShuttleVelocity) const
{
    TViewPortInterface* viewport = cpointer().get_viewport();
    if (!viewport) {
        return {};
    }

    int stepX = qRound(horizontalShuttleVelocity * viewport->viewport_width() * MAX_STEP_FRACTION);
    int stepY = qRound(verticalShuttleVelocity * viewport->viewport_height() * MAX_STEP_FRACTION);

    return QPoint(clamp_horizontal_step(shuttleable, stepX), stepY);
}

int TViewportScroller::clamp_horizontal_step(TShuttleableController* shuttleable, int stepPixels) const
{
    if (!shuttleable || stepPixels == 0) {
        return stepPixels;
    }

    TSelectionTimeBounds bounds = shuttleable->get_time_line_entity_constraints();
    if (!bounds.isValid()) {
        return stepPixels;
    }

    TContextPointer& pointer = cpointer();
    TViewPortInterface* viewport = pointer.get_viewport();
    qint64 scaleFactor = viewport->get_timeref_scale_factor();
    int viewportWidth = viewport->viewport_width();
    int viewportLeft = pointer.scene_x() - pointer.mouse_viewport_x();

    // The controller's time range stays at least this far inside the viewport,
    // so its boundary never moves into the shuttle zone
    int margin = static_cast<int>(viewportWidth * shuttleable->shuttle_range_x());

    // Clamp the step instead of refusing it: otherwise a smaller step (mouse less
    // deep in the shuttle zone) could still pass once a larger one was refused,
    // and the view would creep past the boundary
    if (stepPixels < 0) {
        int startPixel = static_cast<int>(bounds.start.universal_frame() / scaleFactor);
        int minimumViewportLeft = startPixel - margin;
        int room = minimumViewportLeft - viewportLeft;   // <= 0: room to scroll left
        return room >= 0 ? 0 : std::max(stepPixels, room);
    }

    int endPixel = static_cast<int>(bounds.end.universal_frame() / scaleFactor);
    int maximumViewportLeft = endPixel + margin - viewportWidth;
    int room = maximumViewportLeft - viewportLeft;       // >= 0: room to scroll right
    return room <= 0 ? 0 : std::min(stepPixels, room);
}
