// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The objects of one project by id (doc/design/object-ids.md). Two indexes:
// byId for every object, byNumber for the objects with a number from the
// sequence. An object derived from an owner (a master, the bounce track) is in
// byId only; find_owner() gives its owner from its number. TObjectFactory
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

#include <functional>

class TObjectPool : public QObject
{
public:
    // root is the owner of number 0, the project
    explicit TObjectPool(QObject* root = nullptr)
        : m_root(root)
    {
    }

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

    // An object derived from its owner. owner gives its owner as it is now, for
    // the invariant find_owner(id) == owner()
    template <typename Tag>
    bool add_owned(TObjectId<Tag> id, typename Tag::Object* object, std::function<QObject*()> owner)
        requires(std::has_single_bit(Tag::Subtypes))
    {
        if (!insert(id.to_storage(), object)) {
            return false;
        }
        m_ownerOf.insert(id.to_storage(), std::move(owner));
        return true;
    }

    template <typename Tag>
    void remove(TObjectId<Tag> id)
    {
        Q_ASSERT(QThread::isMainThread());
        const uint64_t value = id.to_storage();
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
    QObject* find_raw(uint64_t value) const { return m_byId.value(value); }

    // The owner of an id derived from it: the object with that number, the
    // root for number 0
    QObject* find_owner(uint64_t value) const
    {
        const uint64_t number = TObjectIds::sequence_of(value);
        return number == 0 ? m_root : m_byNumber.value(number);
    }

    // The id of an object in the pool, 0 when it has none
    uint64_t id_of(const QObject* object) const { return m_idOf.value(object); }

    bool contains(uint64_t value) const { return m_byId.contains(value); }
    // Whether an object with a counter id already has this value's number
    bool has_number(uint64_t value) const { return m_byNumber.contains(TObjectIds::sequence_of(value)); }
    qsizetype count() const { return m_byId.size(); }

    // The derived ids whose object is not with the owner its number names: the
    // invariant find_owner(id) == owner(), for the load check and the tests
    QList<uint64_t> owners_out_of_place() const
    {
        QList<uint64_t> wrong;
        for (auto it = m_ownerOf.cbegin(); it != m_ownerOf.cend(); ++it) {
            if (find_owner(it.key()) != it.value()()) {
                wrong.append(it.key());
            }
        }
        return wrong;
    }

private:
    bool insert(uint64_t value, QObject* object)
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
    void erase(uint64_t value)
    {
        if (QObject* object = m_byId.take(value)) {
            m_idOf.remove(object);
            m_ownerOf.remove(value);
            const uint64_t number = TObjectIds::sequence_of(value);
            if (m_byNumber.value(number) == object) {
                m_byNumber.remove(number);
            }
        }
    }

    QObject* m_root;
    QHash<uint64_t, QObject*> m_byId;
    QHash<uint64_t, QObject*> m_byNumber;
    QHash<const QObject*, uint64_t> m_idOf;
    QHash<uint64_t, std::function<QObject*()>> m_ownerOf;
    QHash<uint64_t, QMetaObject::Connection> m_destroyed;
};
