/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TTimeLineEntity.h"
#include <QList>

struct TSplitMutationResult {
    QList<TTimeLineEntity*> toAdd;
    QList<TTimeLineEntity*> toRemove;
    inline bool isEmpty() const { return toAdd.isEmpty() && toRemove.isEmpty(); }
};

class TSplitableTimeLineEntity : virtual public TTimeLineEntity
{
public:
    virtual ~TSplitableTimeLineEntity() override = default;

    virtual bool validate_split_boundary(const TTimeRef& splitLocation) const = 0;

    virtual TSplitMutationResult calculate_split_mutation(const TTimeRef& splitLocation) = 0;
};
