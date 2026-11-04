// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The one place where objects with an id are made (doc/design/object-ids.md).
// make<T>() makes a new object with a new number from the project's sequence,
// load<T>() makes one from its element in the project file with the id saved
// there, make_owned<T, Tag>() makes an object that belongs one to one to its
// owner in a fixed role (a master, the bounce track) with an id derived from
// the owner's number. All enroll it in the project's pool. Owned by the project.
//
// The id is decided before the object is built: a class whose constructor takes
// its id first gets it there (a song, which makes its master and bounce track
// with ids derived from its own); the others get it right after.
//
// A class with an id has, per class:
//   using IdTag = AudioTrackTag;        the exact tag it is made under
//   friend class TObjectFactory;        the factory sets its id
//   an m_objectId member of a tag the IdTag fits in
//   QObject* id_owner() const;          its owner now: the song of a track, the
//                                       track of a clip, nullptr for none
// A class made under more than one tag (the masters) has IdTag void; its maker
// names the tag.
//
// In the project file an id is bare hex: "objectid" for the object's own,
// "owner" for its owner's. The structure of the file places an object; "owner"
// only checks it, and so does "objectid" of a derived object.
//
// GUI thread only, like the pool and the sequence.

#include "TLog.h"
#include "TObjectId.h"
#include "TObjectPool.h"

#include <QDomElement>
#include <QList>
#include <QString>

#include <functional>
#include <type_traits>
#include <utility>

class TObjectFactory
{
public:
    // A loaded id this close to the end of the sequence is refused as damaged
    static constexpr uint64_t SequenceReserve = 1ull << 32;

    TObjectFactory(TObjectIdGenerator& ids, TObjectPool& pool)
        : m_ids(ids)
        , m_pool(pool)
    {
    }

    // A new object: a new number from the sequence
    template <typename T, typename Tag = typename T::IdTag, typename... Args>
    T* make(Args&&... args)
    {
        return build<T>(TObjectId<Tag>::create(m_ids), std::forward<Args>(args)...);
    }

    // An object from its element in the project file: the id saved there when
    // it fits the tag and no other object has it; else a new one, with a
    // warning when the file had one. Its "owner" is kept for
    // check_owner_references(), once the whole file is loaded
    template <typename T, typename Tag = typename T::IdTag, typename... Args>
    T* load(const QDomElement& element, Args&&... args)
    {
        T* object;
        const std::optional<TObjectId<Tag>> loaded = read<Tag>(element, ObjectIdAttribute);
        if (loaded && !m_pool.contains(loaded->to_storage()) && !m_pool.has_number(loaded->to_storage())) {
            object = build<T>(*loaded, std::forward<Args>(args)...);
        } else {
            if (element.hasAttribute(ObjectIdAttribute)) {
                TLOG_W("ids", "the {} id {} in the project file is damaged or used twice: a new one", Tag::Name,
                       text_of(element, ObjectIdAttribute));
                ++m_warnings;
            }
            object = make<T, Tag>(std::forward<Args>(args)...);
        }
        if (element.hasAttribute(OwnerAttribute)) {
            m_ownerReferences.append({object, text_of(element, OwnerAttribute), [object]() { return object->id_owner(); }});
        }
        return object;
    }

    // An object that belongs one to one to its owner in a fixed role: its id is
    // the role's tag on the owner's number (0 for the project, which has none).
    // Unique, as only this role uses this subtype and each owner's number is
    // unique; the same after every load, as it follows from the owner. The
    // owner gives its number, as it is not in the pool yet while it makes its
    // parts in its constructor
    template <typename T, typename Tag, typename... Args>
    T* make_owned(uint64_t ownerNumber, Args&&... args)
        requires(Tag::FromOwner && std::has_single_bit(Tag::Subtypes))
    {
        const std::optional<TObjectId<Tag>> id = TObjectId<Tag>::from_storage(owned_value<Tag>(ownerNumber));
        Q_ASSERT(id);
        T* object = construct<T>(*id, std::forward<Args>(args)...);
        [[maybe_unused]] const bool added = m_pool.add_owned(*id, object, [object]() { return object->id_owner(); });
        Q_ASSERT(added);
        return object;
    }

    // At load, for a derived object the structure placed: its saved id and
    // owner only check it. The owner wins: a difference is a warning, the id
    // stays derived. ownerId is 0 for the project. The number of warnings
    template <typename Tag, typename T>
    int check_owned(T* object, const QDomElement& element, uint64_t ownerId)
    {
        int warnings = 0;
        const uint64_t own = object->m_objectId.to_storage();
        if (element.hasAttribute(ObjectIdAttribute) && read_value(element, ObjectIdAttribute) != own) {
            TLOG_W("ids", "the saved id {} of a {} does not follow from its owner: it stays {}",
                   text_of(element, ObjectIdAttribute), Tag::Name, TObjectIds::to_hex(own));
            ++warnings;
        }
        if (element.hasAttribute(OwnerAttribute) && read_value(element, OwnerAttribute) != ownerId) {
            TLOG_W("ids", "the owner {} of a {} in the project file is not the one it belongs to",
                   text_of(element, OwnerAttribute), Tag::Name);
            ++warnings;
        }
        m_warnings += warnings;
        return warnings;
    }

    // After a load: every "owner" against the owner the structure gave. Without
    // the attribute nothing is said (a clip on no track). An owner that does
    // not exist, or another than the structure's, is a warning; the structure
    // wins and the reference is dropped. The number of warnings
    int check_owner_references()
    {
        int warnings = 0;
        for (const OwnerReference& reference : std::as_const(m_ownerReferences)) {
            const uint64_t named = read_value_text(reference.text);
            QObject* actual = reference.owner();
            if (named == 0 || !m_pool.contains(named)) {
                TLOG_W("ids", "the owner {} of {} does not exist: the reference is dropped", reference.text,
                       TObjectIds::to_string(m_pool.id_of(reference.object)));
                ++warnings;
            } else if (m_pool.find_raw(named) != actual) {
                TLOG_W("ids", "the owner {} of {} is not where the project file puts it: the file's structure wins",
                       reference.text, TObjectIds::to_string(m_pool.id_of(reference.object)));
                ++warnings;
            }
        }
        m_ownerReferences.clear();
        m_warnings += warnings;
        return warnings;
    }

    // Before a load: the counter above the saved next number and above every
    // number saved under root, so an object made during the load (its id
    // missing or damaged) never takes the number of an element further on. A
    // damaged next number (no number, or within the reserve) is ignored with a
    // warning; an id within the reserve is damaged and does not count
    void restore_sequence(const QDomElement& root, const QDomElement& properties)
    {
        bool isNumber = false;
        uint64_t savedNext = properties.attribute(NextObjectIdAttribute).toULongLong(&isNumber);
        if (properties.hasAttribute(NextObjectIdAttribute) && (!isNumber || savedNext > SequenceMask - SequenceReserve)) {
            TLOG_W("ids", "the next object id '{}' in the project file is damaged: ignored",
                   text_of(properties, NextObjectIdAttribute));
            ++m_warnings;
            savedNext = 0;
        }
        m_ids.restore(savedNext, highest_saved_number(root));
    }

    // In the project's save: the counter's next number
    void write_sequence(QDomElement& properties) const
    {
        properties.setAttribute(NextObjectIdAttribute, QString::number(m_ids.next()));
    }

    // The warnings of the load checks since the last call, for the load's
    // summary in the log
    int take_warning_count() { return std::exchange(m_warnings, 0); }

    // In get_state(): the object's own id and its owner's, next to the old "id"
    template <typename Tag>
    static void write(QDomElement& element, TObjectId<Tag> id)
    {
        element.setAttribute(ObjectIdAttribute, QString::fromStdString(id.to_hex()));
    }
    template <typename Tag>
    static void write_owner(QDomElement& element, TObjectId<Tag> owner)
    {
        element.setAttribute(OwnerAttribute, QString::fromStdString(owner.to_hex()));
    }

private:
    static constexpr const char* ObjectIdAttribute = "objectid";
    static constexpr const char* OwnerAttribute = "owner";
    static constexpr const char* NextObjectIdAttribute = "nextobjectid";
    static constexpr uint64_t SequenceMask = TObjectIds::SequenceMask;

    struct OwnerReference
    {
        QObject* object;
        std::string text;
        std::function<QObject*()> owner;
    };

    template <typename Tag>
    static constexpr uint64_t owned_value(uint64_t ownerNumber)
    {
        const uint8_t subtype = uint8_t(std::countr_zero(Tag::Subtypes));
        return (uint64_t(TObjectIds::make_tag(Tag::Kind, subtype)) << TObjectIds::SequenceBits) | ownerNumber;
    }

    template <typename T, typename Tag, typename... Args>
    T* construct(TObjectId<Tag> id, Args&&... args)
    {
        T* object;
        if constexpr (std::is_constructible_v<T, TObjectId<Tag>, Args&&...>) {
            object = new T(id, std::forward<Args>(args)...);
        } else {
            object = new T(std::forward<Args>(args)...);
            object->m_objectId = id;
        }
        return object;
    }

    template <typename T, typename Tag, typename... Args>
    T* build(TObjectId<Tag> id, Args&&... args)
    {
        T* object = construct<T>(id, std::forward<Args>(args)...);
        [[maybe_unused]] const bool added = m_pool.add(id, object);
        Q_ASSERT(added);
        return object;
    }

    static std::string text_of(const QDomElement& element, const char* attribute)
    {
        return element.attribute(attribute).toStdString();
    }

    // A bare hex id, or one with a prefix; 0 when it is no id
    static uint64_t raw_value(const QString& value)
    {
        bool ok = false;
        const uint64_t raw = value.mid(value.indexOf(':') + 1).toULongLong(&ok, 16);
        return ok ? raw : 0;
    }

    // The highest number of an objectid under element, ids within the reserve
    // left out
    static uint64_t highest_saved_number(const QDomElement& element)
    {
        const uint64_t number = TObjectIds::sequence_of(raw_value(element.attribute(ObjectIdAttribute)));
        uint64_t highest = number <= SequenceMask - SequenceReserve ? number : 0;
        for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
            highest = std::max(highest, highest_saved_number(child));
        }
        return highest;
    }

    // A bare hex id. A prefix ("audiotrack:", the form of the log and the
    // agent) is checked against the id's tag and then ignored. 0 when it is no id
    uint64_t read_value_text(const std::string& text)
    {
        const QString value = QString::fromStdString(text);
        const qsizetype colon = value.indexOf(':');
        const uint64_t raw = raw_value(value);
        if (raw == 0) {
            return 0;
        }
        if (colon >= 0 && value.left(colon) != QLatin1String(TObjectIds::name_of(TObjectIds::tag_of(raw)))) {
            TLOG_W("ids", "the id {} in the project file names another kind than its number has", text);
            ++m_warnings;
        }
        return raw;
    }

    uint64_t read_value(const QDomElement& element, const char* attribute)
    {
        return read_value_text(text_of(element, attribute));
    }

    template <typename Tag>
    std::optional<TObjectId<Tag>> read(const QDomElement& element, const char* attribute)
    {
        const uint64_t value = read_value(element, attribute);
        if (TObjectIds::sequence_of(value) > SequenceMask - SequenceReserve) {
            return std::nullopt;
        }
        return TObjectId<Tag>::from_storage(value);
    }

    TObjectIdGenerator& m_ids;
    TObjectPool& m_pool;
    QList<OwnerReference> m_ownerReferences;
    int m_warnings{0};
};
