/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#include "TViewPortScroller.h"
#include "TContextPointer.h"
#include "TShuttleableController.h"
#include "TSelectionTimeBounds.h"

TViewportScroller::TViewportScroller(TViewPortInterface::ScrollModes modes)
    : m_scrollModes(modes)
{
    m_shuttleCurve.setType(QEasingCurve::InQuad);
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

    bool hasActiveShuttle = false;

    if (m_scrollModes.testFlag(TViewPortInterface::Horizontal)) {
        calculatedHorizontalShuttleVelocity = determine_shuttle_vector(
            pointer.mouse_viewport_x(),
            viewport->viewport_width(),
            shuttleable->shuttle_range_x()
            );
        if (!qFuzzyIsNull(calculatedHorizontalShuttleVelocity)) {
            hasActiveShuttle = true;
        }
    }

    if (m_scrollModes.testFlag(TViewPortInterface::Vertical)) {
        calculatedVerticalShuttleVelocity = determine_shuttle_vector(
            pointer.mouse_viewport_y(),
            viewport->viewport_height(),
            shuttleable->shuttle_range_y()
            );
        if (!qFuzzyIsNull(calculatedVerticalShuttleVelocity)) {
            hasActiveShuttle = true;
        }
    }

    if (hasActiveShuttle && !qFuzzyIsNull(calculatedHorizontalShuttleVelocity)) {
        qint64 scaleFactorFramesPerPixel = viewport->get_timeref_scale_factor();

        if (!enforce_horizontal_domain_bounds(shuttleable, calculatedHorizontalShuttleVelocity, scaleFactorFramesPerPixel)) {
            hasActiveShuttle = false;
        }
    }

    return hasActiveShuttle;
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
        return m_shuttleCurve.valueForProgress(accelerationProgress) * movementDirection;
    }

    return 0.0;
}

bool TViewportScroller::enforce_horizontal_domain_bounds(TShuttleableController* shuttleable, qreal horizontalVelocity, qint64 scaleFactor)
{
    if (!shuttleable) {
        return true;
    }

    TSelectionTimeBounds bounds = shuttleable->get_time_line_entity_constraints();
    if (!bounds.isValid()) {
        return true;
    }

    TContextPointer& pointer = cpointer();
    int viewWidthPixels = pointer.get_viewport()->viewport_width();

    int scrollStepPixels = qRound(horizontalVelocity * SCROLL_VELOCITY_SCALER);
    int currentViewportLeftPixel = pointer.scene_x() - pointer.mouse_viewport_x();
    int nextViewportLeftPixel = currentViewportLeftPixel + scrollStepPixels;

    int deactivationZoneMarginPixels = static_cast<int>(viewWidthPixels * shuttleable->shuttle_range_x());

    if (horizontalVelocity < 0.0) {
        int domainStartBoundaryPixel = static_cast<int>(bounds.start.universal_frame() / scaleFactor);
        int leftDeactivationLineTargetPixel = nextViewportLeftPixel + deactivationZoneMarginPixels;

        if (domainStartBoundaryPixel >= leftDeactivationLineTargetPixel) {
            return false;
        }
    }

    if (horizontalVelocity > 0.0) {
        int domainEndBoundaryPixel = static_cast<int>(bounds.end.universal_frame() / scaleFactor);
        int nextViewportRightPixel = nextViewportLeftPixel + viewWidthPixels;
        int rightDeactivationLineTargetPixel = nextViewportRightPixel - deactivationZoneMarginPixels;

        if (domainEndBoundaryPixel <= rightDeactivationLineTargetPixel) {
            return false;
        }
    }

    return true;
}
