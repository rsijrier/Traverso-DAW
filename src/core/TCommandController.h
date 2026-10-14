/*
    Copyright (C) 2005-2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include <QObject>
#include <QString>
#include <QUndoCommand>
#include <memory>

#include "TInteractionCursors.h"
#include "TSelectionTimeBounds.h"
#include "TViewPortInterface.h"

class QUndoStack;
class TContextItem;

// Outcome of TCommandController::commit()
struct TCommitResult
{
    enum class Status {
        Committed,      // command holds the change, it goes onto the undo stack
        NothingToDo,    // nothing changed, there is nothing to push
        Failed          // the change could not be made, reason tells the user why
    };

    Status                        status{Status::NothingToDo};
    std::unique_ptr<QUndoCommand> command;
    QString                       reason;   // shown to the user, so translated

    // A null command means nothing changed
    static TCommitResult committed(std::unique_ptr<QUndoCommand> command)
    {
        TCommitResult result;
        if (command) {
            result.status = Status::Committed;
            result.command = std::move(command);
        }
        return result;
    }

    static TCommitResult nothing_to_do() { return {}; }

    static TCommitResult failed(QString reason)
    {
        TCommitResult result;
        result.status = Status::Failed;
        result.reason = std::move(reason);
        return result;
    }
};

/**
 * @brief Base class of the controllers that replace the legacy TCommand classes.
 *
 * A controller handles one user interaction and, when finished, hands back the
 * change as an undo command. TInputEventDispatcher drives it:
 *
 * - a hold controller (is_hold() returns true) gets begin_hold(), then update()
 *   for every input change, and finally commit() or cancel()
 * - any other controller is committed right after it was created
 *
 * Controllers override only what they need, the defaults describe a controller
 * without a hold phase, without edge scrolling and with the default cursor.
 */
class TCommandController : public QObject
{
    Q_OBJECT

public:
    explicit TCommandController(TContextItem* item)
        : QObject(nullptr)
        , m_contextItem(item) {}

    ~TCommandController() override = default;

    // Lifecycle

    virtual bool is_hold() const { return false; }

    // false: the hold does not start and the controller is discarded
    [[nodiscard]] virtual bool begin_hold() { return true; }

    // The input changed while holding
    virtual void update() {}

    virtual void cancel() {}

    // Ends the interaction and returns its change. The reason of a failed
    // commit is shown to the user with tInformUser(). Feedback about an invalid
    // state while holding belongs in the controller's cursor data.
    [[nodiscard]] virtual TCommitResult commit() = 0;

    // Presentation

    virtual TraversoDAW::CursorType cursor_type() const { return TraversoDAW::CursorType::Default; }
    virtual bool canvas_cursor_follows_mouse_cursor() const { return true; }

    // Edge scrolling while holding

    virtual TViewPortInterface::ScrollModes scrolling_mode() const { return TViewPortInterface::None; }

    // The time range the view may scroll to while holding, invalid means no limit
    virtual TSelectionTimeBounds time_constraints() const { return {}; }

    QUndoStack* get_context_item_undo_stack() const;

protected:
    TContextItem* m_contextItem{nullptr};
};
