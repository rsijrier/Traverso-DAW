/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include <QtPlugin>

class QUndoCommand;

class TTransactional
{
public:
    virtual ~TTransactional() = default;

    virtual int prepare_actions() = 0;
    virtual QUndoCommand* get_undo_command() = 0;
};

Q_DECLARE_INTERFACE(TTransactional, "org.traverso.TTransactional")
