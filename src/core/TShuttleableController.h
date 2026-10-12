/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TViewPortInterface.h"
#include <QtPlugin>
struct TSelectionTimeBounds;

class TTimeLineEntity;

class TShuttleableController
{
public:
    virtual ~TShuttleableController() = default;

    virtual TViewPortInterface::ScrollModes scrolling_mode() const = 0;

    virtual qreal shuttle_range_x() const { return 0.18; }
    virtual qreal shuttle_range_y() const { return 0.18; }
    virtual const TSelectionTimeBounds get_time_line_entity_constraints() = 0;
};

Q_DECLARE_INTERFACE(TShuttleableController, "org.traverso.TShuttleableController")
