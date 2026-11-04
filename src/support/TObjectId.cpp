// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TObjectId.h"

#include "TLog.h"

#include <QThread>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

uint64_t TObjectIdGenerator::create(uint16_t tag)
{
    // The contract (doc/design/object-ids.md): the GUI thread makes ids. The
    // mutex keeps the sequence whole if that is ever broken in a release build
    Q_ASSERT(QThread::isMainThread());
    Q_ASSERT(TObjectIds::subtype_of(tag) < TObjectIds::SubtypeCount);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_next > TObjectIds::SequenceMask) {
        TLOG_E("ids", "the sequence of object ids is used up");
        Q_ASSERT(false);
        return 0;
    }
    return (uint64_t(tag) << TObjectIds::SequenceBits) | m_next++;
}

void TObjectIdGenerator::restore(uint64_t savedNext, uint64_t highestLoaded)
{
    Q_ASSERT(QThread::isMainThread());
    std::lock_guard<std::mutex> lock(m_mutex);
    m_next = std::max({m_next, savedNext, highestLoaded + 1});
}

uint64_t TObjectIdGenerator::next() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_next;
}

// Indexed by kind and subtype: 256 kinds of 64 subtypes
static std::array<std::atomic<const char*>, 256 * TObjectIds::SubtypeCount>& names()
{
    static std::array<std::atomic<const char*>, 256 * TObjectIds::SubtypeCount> table{};
    return table;
}

static size_t name_index(uint16_t tag)
{
    return size_t(TObjectIds::kind_of(tag)) * TObjectIds::SubtypeCount + (TObjectIds::subtype_of(tag) % TObjectIds::SubtypeCount);
}

void TObjectIds::register_name(uint16_t tag, const char* name)
{
    names()[name_index(tag)].store(name, std::memory_order_release);
}

const char* TObjectIds::name_of(uint16_t tag)
{
    const char* name = names()[name_index(tag)].load(std::memory_order_acquire);
    return name ? name : "id";
}

std::string TObjectIds::to_string(uint64_t value)
{
    char text[64];
    std::snprintf(text, sizeof(text), "%s:%016llx", name_of(tag_of(value)), static_cast<unsigned long long>(value));
    return text;
}

std::string TObjectIds::to_hex(uint64_t value)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}
