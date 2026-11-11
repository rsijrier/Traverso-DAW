// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The peers of one project by id (doc/design/object-ids.md): every object
// whose tag has InPool (songs, tracks with the masters, clips, markers, sends,
// sources, workspaces). Parts (curves, the ruler) are not here: they are found
// through their owner. Two indexes: byId for every peer, byNumber for the
// peers with a number from the sequence. A peer derived from an owner (a
// master, the bounce track) is in byId only: its number is its owner's.
// TObjectFactory enrolls a peer when it makes or loads it, the maker of a
// derived peer enrolls it; it leaves when it is destroyed.
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
    // A peer with a number from the sequence. False when the id is there
    // already, its number is used by another object, or it is invalid
    template <typename Tag>
    bool add(TObjectId<Tag> id, typename Tag::Object* object)
        requires(Tag::InPool && !Tag::FromOwner && std::has_single_bit(Tag::Subtypes))
    {
        if (has_number(id.to_storage()) || !insert(id.to_storage(), object)) {
            return false;
        }
        m_byNumber.insert(TObjectIds::sequence_of(id.to_storage()), object);
        return true;
    }

    // A peer derived from its owner (a master, the bounce track), with the id
    // it was made with: its number is its owner's, so it is not in byNumber.
    // False when the id is there already: two owners with one number in the
    // same role, the derivation rule does not hold for one of them
    template <typename Tag>
    bool add(typename Tag::Object* object)
        requires(Tag::InPool && Tag::FromOwner && std::has_single_bit(Tag::Subtypes))
    {
        Q_ASSERT(object);
        const std::optional<TObjectId<Tag>> id = TObjectId<Tag>::from_storage(object->object_id().to_storage());
        Q_ASSERT(id);
        return insert(id->to_storage(), object);
    }

    template <typename Tag>
    void remove(TObjectId<Tag> id)
        requires(Tag::InPool)
    {
        Q_ASSERT(QThread::isMainThread());
        const object_id_value_t value = id.to_storage();
        disconnect(m_destroyed.take(value));
        erase(value);
    }

    // The object, when it has this id; else nullptr
    template <typename Tag>
    typename Tag::Object* find(TObjectId<Tag> id) const
        requires(Tag::InPool)
    {
        Q_ASSERT(QThread::isMainThread());
        return static_cast<typename Tag::Object*>(find_raw(id.to_storage()));
    }

    // Without a domain, for QML and the log: the caller checks the tag
    QObject* find_raw(object_id_value_t value) const { return m_byId.value(value); }


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
            const object_id_value_t number = TObjectIds::sequence_of(value);
            if (m_byNumber.value(number) == object) {
                m_byNumber.remove(number);
            }
        }
    }

    QHash<object_id_value_t, QObject*> m_byId;
    QHash<object_id_value_t, QObject*> m_byNumber;
    QHash<object_id_value_t, QMetaObject::Connection> m_destroyed;
};
