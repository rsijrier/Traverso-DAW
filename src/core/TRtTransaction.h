/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include <QList>
#include <memory>

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
    // GUI thread: register this list as changed in the current transaction.
    void mark_dirty();

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
 * Create one on the stack in the GUI thread around the code that changes the
 * state. Transactions nest: an inner transaction joins the outermost one, and
 * only the outermost transaction publishes when it goes out of scope.
 *
 *     {
 *         TRtTransaction transaction;
 *         track->apply_clip_mutation(clipsToAdd, clipsToRemove);
 *     } // one event posted for everything changed above
 *
 * A TRtList changed while no transaction is open publishes on its own, and
 * warns in debug builds, so such code paths can be found and wrapped.
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

    // The innermost open transaction in the GUI thread, or nullptr.
    static TRtTransaction* current();

    void mark_dirty(TRtListBase* list);

private:
    void commit();

    TRtTransaction*      m_outer{nullptr};
    QList<TRtListBase*>  m_dirtyLists;
};
