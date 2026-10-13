/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TViewPortInterface.h"
#include <QEasingCurve>
#include <QPoint>

class TShuttleableController;

class TViewportScroller final
{
public:
    explicit TViewportScroller(TViewPortInterface::ScrollModes modes = TViewPortInterface::None);
    ~TViewportScroller() = default;

    bool calculate_shuttle_intensity(TShuttleableController* shuttleable,
                                     qreal& calculatedHorizontalShuttleVelocity,
                                     qreal& calculatedVerticalShuttleVelocity);

    void set_scroll_modes(TViewPortInterface::ScrollModes modes) { m_scrollModes = modes; }

    // Pixels to scroll per shuttle timer tick for the given velocities, the
    // horizontal step is clamped to the controller's time range
    QPoint calculate_scroll_step(TShuttleableController* shuttleable,
                                 qreal horizontalShuttleVelocity,
                                 qreal verticalShuttleVelocity) const;

    // Same as the legacy TMoveCommand shuttle timer
    static constexpr int SHUTTLE_INTERVAL_MS = 40;

private:
    qreal determine_shuttle_vector(int mouseViewportPosition, int viewportDimension, qreal triggerZoneWidth);

    int clamp_horizontal_step(TShuttleableController* shuttleable, int stepPixels) const;

    TViewPortInterface::ScrollModes m_scrollModes;
    QEasingCurve                    m_shuttleCurve;

    // At full velocity a tick scrolls this fraction of the viewport size, the
    // same maximum speed as the legacy TMoveCommand shuttle (about 48 pixels
    // per tick on a 1500 pixels wide viewport)
    static constexpr qreal MAX_STEP_FRACTION = 0.18 * 0.18;
};
