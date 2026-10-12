/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TViewPortInterface.h"
#include <QEasingCurve>

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

private:
    qreal determine_shuttle_vector(int mouseViewportPosition, int viewportDimension, qreal triggerZoneWidth);

    bool enforce_horizontal_domain_bounds(TShuttleableController* shuttleable, qreal horizontalVelocity, qint64 scaleFactor);

    TViewPortInterface::ScrollModes m_scrollModes;
    QEasingCurve                    m_shuttleCurve;

    static constexpr qreal SCROLL_VELOCITY_SCALER = 3.0;
};
