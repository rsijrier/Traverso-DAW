/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include <QtPlugin>

class TContextItem;

class TSnapableTimeController
{
public:
    virtual ~TSnapableTimeController() = default;

    virtual void move_left() = 0;
    virtual void move_right() = 0;
    virtual void next_snap_pos() = 0;
    virtual void prev_snap_pos() = 0;

    void execute_snap_toggle(TContextItem* contextItem);
};

Q_DECLARE_INTERFACE(TSnapableTimeController, "org.traverso.TSnapableTimeController")
