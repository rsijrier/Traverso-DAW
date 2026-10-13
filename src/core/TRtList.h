/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TRtTransaction.h"

#include <QList>
#include <algorithm>
#include <functional>
#include <vector>

/**
 * @brief A list of items that is read by the audio thread.
 *
 * The GUI thread owns the list: it is the single source of truth and only the
 * GUI thread changes it. The audio thread reads an immutable snapshot of it.
 * Every change registers the list in the current TRtTransaction, which builds
 * a new snapshot in the GUI thread and lets the audio thread swap it in.
 *
 * Replaces TRealTimeLinkedList: the audio thread only swaps a pointer, so the
 * cost in the audio thread does not depend on the size of the list.
 *
 * A pending transaction step refers to the list, so the list (and its owner)
 * must not be destroyed before the audio thread applied that step.
 */
template <class T>
class TRtList final : public TRtListBase
{
public:
    TRtList() : m_rtItems(new std::vector<T*>()) {}
    ~TRtList() override { delete m_rtItems; }

    TRtList(const TRtList&) = delete;
    TRtList& operator=(const TRtList&) = delete;

    // GUI thread

    const QList<T*>& items() const
    {
        Q_ASSERT_X(is_gui_thread(), "TRtList::items", "GUI side used outside the GUI thread");
        return m_items;
    }

    void add(T* item)
    {
        Q_ASSERT_X(is_gui_thread(), "TRtList::add", "GUI side used outside the GUI thread");
        m_items.append(item);
        sort();
        mark_dirty();
    }

    void remove(T* item)
    {
        Q_ASSERT_X(is_gui_thread(), "TRtList::remove", "GUI side used outside the GUI thread");
        m_items.removeAll(item);
        mark_dirty();
    }

    // Keeps the list sorted, e.g. clips by start location
    void set_sort_function(std::function<bool(const T*, const T*)> lessThan)
    {
        Q_ASSERT_X(is_gui_thread(), "TRtList::set_sort_function", "GUI side used outside the GUI thread");
        m_lessThan = std::move(lessThan);
        sort();
    }

    // Call after the sort order of items changed, e.g. a clip moved
    void resort()
    {
        Q_ASSERT_X(is_gui_thread(), "TRtList::resort", "GUI side used outside the GUI thread");
        sort();
        mark_dirty();
    }

    // Audio thread

    const std::vector<T*>& rt_items() const
    {
        Q_ASSERT_X(!is_gui_thread(), "TRtList::rt_items", "audio thread side used in the GUI thread");
        return *m_rtItems;
    }

private:
    class SwapStep final : public TRtStep
    {
    public:
        SwapStep(std::vector<T*>** slot, std::vector<T*>* snapshot)
            : m_slot(slot), m_snapshot(snapshot) {}

        // Before apply this deletes the unused new snapshot, after apply the old one
        ~SwapStep() override { delete m_snapshot; }

        void apply_in_audio_thread() override { std::swap(*m_slot, m_snapshot); }

    private:
        std::vector<T*>** m_slot;
        std::vector<T*>*  m_snapshot;
    };

    std::unique_ptr<TRtStep> create_publish_step() override
    {
        return std::make_unique<SwapStep>(&m_rtItems, new std::vector<T*>(m_items.begin(), m_items.end()));
    }

    void sort()
    {
        if (m_lessThan) {
            std::stable_sort(m_items.begin(), m_items.end(), m_lessThan);
        }
    }

    QList<T*>                              m_items;
    std::vector<T*>*                       m_rtItems;
    std::function<bool(const T*, const T*)> m_lessThan;
};
