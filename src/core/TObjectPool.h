// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The objects of one project by id (doc/design/object-ids.md). Two indexes:
// byId for every object, byNumber for the objects with a number from the
// sequence. An object derived from an owner (a master, the bounce track) is in
// byId only: its number is its owner's. TObjectFactory
// enrolls an object when it makes or loads it; it leaves when it is destroyed.
// Whether an object is in the project's tree now is not the index's question:
// an edit asks the object. Owned by the project, no singleton.
//
// The pool knows no domain: find() casts to the Object type its tag names, so
// the class definition is needed where find() is called (the controller), not
// here. Domains know the pool, the pool knows no domains.
//
// GUI thread only: ids are made, enrolled and found outside the real-time path.

#include "TObjectId.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QThread>
#include <QtGlobal>


class TObjectPool : public QObject
{
public:
    // An object with a number from the sequence. False when the id is there
    // already, its number is used by another object, or it is invalid
    template <typename Tag>
    bool add(TObjectId<Tag> id, typename Tag::Object* object)
        requires(std::has_single_bit(Tag::Subtypes))
    {
        if (has_number(id.to_storage()) || !insert(id.to_storage(), object)) {
            return false;
        }
        m_byNumber.insert(TObjectIds::sequence_of(id.to_storage()), object);
        return true;
    }

    // An object derived from its owner: its number is its owner's, so it is not
    // in byNumber
    template <typename Tag>
    bool add_owned(TObjectId<Tag> id, typename Tag::Object* object)
        requires(std::has_single_bit(Tag::Subtypes))
    {
        return insert(id.to_storage(), object);
    }

    template <typename Tag>
    void remove(TObjectId<Tag> id)
    {
        Q_ASSERT(QThread::isMainThread());
        const object_id_value_t value = id.to_storage();
        disconnect(m_destroyed.take(value));
        erase(value);
    }

    // The object, when it has this id; else nullptr
    template <typename Tag>
    typename Tag::Object* find(TObjectId<Tag> id) const
    {
        Q_ASSERT(QThread::isMainThread());
        return static_cast<typename Tag::Object*>(find_raw(id.to_storage()));
    }

    // Without a domain, for QML and the log: the caller checks the tag
    QObject* find_raw(object_id_value_t value) const { return m_byId.value(value); }

    // The id of an object in the pool, 0 when it has none
    object_id_value_t id_of(const QObject* object) const { return m_idOf.value(object); }

    bool contains(object_id_value_t value) const { return m_byId.contains(value); }
    // Whether an object with a counter id already has this value's number
    bool has_number(object_id_value_t value) const { return m_byNumber.contains(TObjectIds::sequence_of(value)); }
    qsizetype count() const { return m_byId.size(); }

private:
    bool insert(object_id_value_t value, QObject* object)
    {
        Q_ASSERT(QThread::isMainThread());
        if (value == 0 || object == nullptr || m_byId.contains(value)) {
            return false;
        }
        m_byId.insert(value, object);
        m_idOf.insert(object, value);
        // Out when it is destroyed. The pool is the context: when the pool goes
        // first (a project's own tracks die after its members), so does this
        m_destroyed.insert(value, connect(object, &QObject::destroyed, this, [this, value]() {
            m_destroyed.remove(value);
            erase(value);
        }));
        return true;
    }

    // Removing an id that is not there is a no-op: remove() may come for an
    // object that already left by its destruction
    void erase(object_id_value_t value)
    {
        if (QObject* object = m_byId.take(value)) {
            m_idOf.remove(object);
            const object_id_value_t number = TObjectIds::sequence_of(value);
            if (m_byNumber.value(number) == object) {
                m_byNumber.remove(number);
            }
        }
    }

    QHash<object_id_value_t, QObject*> m_byId;
    QHash<object_id_value_t, QObject*> m_byNumber;
    QHash<const QObject*, object_id_value_t> m_idOf;
    QHash<object_id_value_t, QMetaObject::Connection> m_destroyed;
};
