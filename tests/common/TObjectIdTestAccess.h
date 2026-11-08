// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The tests check the values the project file and the pool hold, which
// TObjectId shows to no one else: it lets this struct see them

#include "TObjectId.h"

#include <optional>

struct TObjectIdTestAccess
{
    template <typename Tag>
    static object_id_value_t value(TObjectId<Tag> id) { return id.to_storage(); }

    template <typename Id>
    static std::optional<Id> from_value(object_id_value_t value) { return Id::from_storage(value); }

    template <typename Id>
    static bool accepts(object_id_value_t value) { return Id::accepts(value); }
};
