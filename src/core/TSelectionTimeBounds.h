/*
    Copyright (C) 2005 - 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TTimeRef.h"

struct TSelectionTimeBounds {
    TTimeRef start{TTimeRef::max_length()};
    TTimeRef end{TTimeRef()};

    inline bool isValid() const {
        return start < end && start != TTimeRef::max_length();
    }
};
