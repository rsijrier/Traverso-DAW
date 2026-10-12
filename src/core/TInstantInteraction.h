/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include <QtPlugin>

class TInstantInteraction
{
public:
    virtual ~TInstantInteraction() = default;
    virtual bool is_hold_command_impl() const { return false; }
};

Q_DECLARE_INTERFACE(TInstantInteraction, "org.traverso.TInstantInteraction")
