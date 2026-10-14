/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include <QList>
#include <cstddef>
#include <memory>

class TRtTransaction;

/**
 * @brief One change to apply in the audio thread as part of a TRtTransaction.
 *
 * Steps are created in the GUI thread and applied in the audio thread, at the
 * start of a process cycle. Afterwards they are deleted in the message queue
 * worker thread (see TAudioThreadMessageQueueEvent), so a step may own memory
 * that has to be freed once the audio thread no longer uses it.
 */
class TRtStep
{
public:
    virtual ~TRtStep() = default;

    // Runs in the audio thread: no allocation, no freeing, no locks.
    virtual void apply_in_audio_thread() = 0;
};

/**
 * @brief Base class for containers that publish their state to the audio
 *        thread through a TRtTransaction, see TRtList.
 */
class TRtListBase
{
public:
    virtual ~TRtListBase() = default;

protected:
    // GUI thread: register this list as changed in transaction.
    void mark_dirty(TRtTransaction& transaction);

    // Used by debug assertions to check that each side of a list is only used
    // from its own thread.
    static bool is_gui_thread();

private:
    friend class TRtTransaction;

    // GUI thread: create the step that publishes the current state.
    virtual std::unique_ptr<TRtStep> create_publish_step() = 0;
};

/**
 * @brief Collects all changes to real-time state made during one user action
 *        and publishes them to the audio thread as a single event.
 *
 * Create one on the stack in the GUI thread and pass it to every function that
 * changes real-time state; a TRtList can only be changed with a transaction.
 * It publishes when it goes out of scope.
 *
 *     {
 *         TRtTransaction transaction;
 *         track->apply_clip_mutation(clipsToAdd, clipsToRemove, transaction);
 *     } // one event posted for everything changed above
 *
 * A transaction created while another one is open joins it, and only the
 * outermost one publishes. Undo commands rely on that: QUndoCommand::redo()
 * takes no arguments, so a command opens its own transaction, which joins
 * the one of a command or hold it runs in.
 *
 * The audio thread always runs (TAudioDevice falls back to the dummy driver),
 * so the event is always posted. Only while the audio device is switching
 * drivers or shutting down are the changes applied directly.
 */
class TRtTransaction
{
public:
    TRtTransaction();
    ~TRtTransaction();

    TRtTransaction(const TRtTransaction&) = delete;
    TRtTransaction& operator=(const TRtTransaction&) = delete;

    // Only on the stack, so it publishes when the change is done instead of
    // living on unnoticed
    static void* operator new(std::size_t) = delete;
    static void* operator new[](std::size_t) = delete;

private:
    friend class TRtListBase;

    void mark_dirty(TRtListBase* list);
    void commit();

    TRtTransaction*      m_outer{nullptr};
    QList<TRtListBase*>  m_dirtyLists;
};
