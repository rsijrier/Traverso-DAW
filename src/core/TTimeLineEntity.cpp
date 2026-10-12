#include "TTimeLineEntity.h"

#include "TTimeRef.h"
#include "TLocation.h"

TTimeRef TTimeLineEntity::get_location_start() const
{
    Q_ASSERT(m_location);
    return m_location->get_start();
}

TTimeRef TTimeLineEntity::get_location_end() const
{
    Q_ASSERT(m_location);
    return m_location->get_end();
}
