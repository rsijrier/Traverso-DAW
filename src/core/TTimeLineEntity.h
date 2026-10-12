/*
    Copyright (C) 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TTimeRef.h"
#include "TLocation.h"
#include <memory>

class TTimeLineEntity
{
public:
    enum Type {
        TypeNoType      = 0x00,
        TypeAudioClip   = 0x01,
        TypeVideoClip   = 0x02,
        TypeMarker      = 0x04,
        TypeCurveNode   = 0x08,
        TypeAll         = 0xFF
    };

    TTimeLineEntity() : m_location(std::make_unique<TLocation>(nullptr)) {}
    virtual ~TTimeLineEntity() = default;

    virtual void move_to_location(const TTimeRef& location) = 0;
    virtual unsigned int get_timeline_entity_type() const = 0;

    TTimeRef get_location_start() const;
    TTimeRef get_location_end() const;
    TLocation* get_location() const { return m_location.get(); }

protected:
    std::unique_ptr<TLocation> m_location;
};

