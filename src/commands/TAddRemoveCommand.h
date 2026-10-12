/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommand.h"
#include <functional>

class TSession;
class TContextItem;

class TAddRemoveCommand : public TCommand
{
    Q_OBJECT

public:
    explicit TAddRemoveCommand(TContextItem* parent,
                               bool historable,
                               std::function<void()> doMethod,
                               std::function<void()> undoMethod,
                               const QString& des);

    TAddRemoveCommand(TContextItem* parent,
                      void* arg,
                      bool historable,
                      TSession* sheet,
                      std::function<void()> doMethod,   std::function<void()> doSignal,
                      std::function<void()> undoMethod, std::function<void()> undoSignal,
                      const QString& des);

    TAddRemoveCommand(TContextItem* parent,
                      TContextItem* item,
                      bool historable,
                      TSession* sheet,
                      std::function<void()> doMethod,   std::function<void()> doSignal,
                      std::function<void()> undoMethod, std::function<void()> undoSignal,
                      const QString& des);

    ~TAddRemoveCommand() override = default;

    bool is_hold_command() const override { return false; }
    int prepare_actions() override { return 1; }

    int do_action() override;
    int undo_action() override;

    void set_instantanious(bool instant);

private:
    int un_redo_action(const std::function<void()>& method, const std::function<void()>& signal);

    TContextItem*         m_parentItem{nullptr};
    std::function<void()> m_doMethod;
    std::function<void()> m_undoMethod;
    std::function<void()> m_doSignal;
    std::function<void()> m_undoSignal;
    bool                  m_instantanious{false};
};
