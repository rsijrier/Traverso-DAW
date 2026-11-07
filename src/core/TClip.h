// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TContextItem.h"
#include "TObjectIdTags.h"
#include "TSplitableTimeLineEntity.h"

#include <type_traits>

// What every kind of clip is: an object with an id on the timeline that can be
// split. Its kinds differ through the virtual functions of the timeline bases
// (move_to_location(), validate_split_boundary(), calculate_split_mutation());
// TAudioClip is the only one now, video and text later
class TClip : public TContextItem, public TSplitableTimeLineEntity
{
    Q_OBJECT

public:
    // The id, given by TObjectFactory when it made or loaded the clip, in the
    // exact tag of its kind (doc/design/object-ids.md)
    ClipId object_id() const
    {
        Q_ASSERT(m_objectId.is_valid());
        return m_objectId;
    }

protected:
    TClip() = default;

private:
    friend class TObjectFactory;
    ClipId m_objectId;
};

static_assert(std::is_base_of_v<TSplitableTimeLineEntity, ClipTag::Object>,
              "find(ClipId) gives something that can be split");
