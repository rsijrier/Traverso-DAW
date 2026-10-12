/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TInteractionCursors.h"
#include <QtPlugin>

class THoldInteraction
{
public:
    virtual ~THoldInteraction() = default;

    virtual int begin_hold() = 0;
    virtual int jog() = 0;
    virtual int finish_hold() = 0;
    virtual void cancel_action() = 0;
    virtual bool canvas_cursor_follows_mouse_cursor() const = 0;
    virtual TraversoDAW::CursorType get_cursor_type() const = 0;
};

Q_DECLARE_INTERFACE(THoldInteraction, "org.traverso.THoldInteraction")
