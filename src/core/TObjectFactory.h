// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The one place where objects with an id are made (doc/design/object-ids.md).
// make<T>() makes a new object with a new number from the project's sequence,
// load<T>() makes one from its element in the project file with the id saved
// there, make_derived<Tag>(owner) makes an object that belongs one to one to
// its owner in a fixed role (a master, the bounce track, the ruler, a fade)
// with an id derived from the owner's number; such an object writes no id and
// no owner, its place in the file and its owner give it. Peers (songs, tracks
// with the masters, clips, markers, sends, sources, workspaces; the tag's
// InPool) are in the project's pool, so find() reaches every living peer by a
// bare id; make() and load() enroll them, the maker of a derived peer enrolls
// it. Parts (curves, the ruler) are not in the pool: they are reached through
// their owner, addressed by (owner id, role). Owned by the project.
//
// The id is decided before the object is built: a class whose constructor takes
// its id first gets it there (a song, which makes its master and bounce track
// with ids derived from its own); the others get it right after.
//
// A class with an id has, per class:
//   using IdTag = AudioTrackTag;        the exact tag it is made under
//   friend class TObjectFactory;        the factory sets its id
//   an m_objectId member of a tag the IdTag fits in
// A class made under more than one tag (the masters) has IdTag void; its maker
// names the tag.
//
// In the project file an id is bare hex: "objectid" for the object's own,
// "owner" for its owner's, and a named attribute for a reference to another
// object (read_reference()). The structure of the file places an object, and
// make_derived() gives a derived object its owner: "owner" is written, not read
// or checked. A flat file (T112) will place objects by it.
//
// GUI thread only, like the pool and the sequence.

#include "TLog.h"
#include "TObjectId.h"
#include "TObjectPool.h"

#include <QDomElement>
#include <QList>
#include <QString>
#include <QStringList>

#include <type_traits>
#include <utility>

// An owner make_derived() accepts: its own id is a counter id (its IdTag is
// exact and not derived from an owner). A class made under several tags (IdTag
// void, the masters) or with a derived id is refused at compile time
template <typename Owner>
concept derivable_owner = requires { typename Owner::IdTag; } && !std::is_void_v<typename Owner::IdTag>
                          && requires { requires !Owner::IdTag::FromOwner; };

class TObjectFactory
{
public:
    // A loaded id this close to the end of the sequence is refused as damaged
    static constexpr object_id_value_t SequenceReserve = 1ull << 32;

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
    // warning when the file had one
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
        return object;
    }

    // An object that belongs one to one to its owner in a fixed role (a master,
    // the bounce track, the ruler, a fade): its id is the role's tag on the
    // owner's number (0 for the project). Unique, as only this role uses this
    // subtype and each owner's number is unique; the same after every load, as
    // it follows from the owner. Made as Tag::Object(id, owner, args...). The
    // owner must have a counter id (or be the project): an owner whose own id
    // is derived shares its number with its owner, so the derivation would not
    // be unique; an object of such an owner gets a counter id. Static, without
    // the pool: a part (ruler, fade) is not in the pool, and the maker of a
    // peer (a master, the bounce track) enrolls it with TObjectPool::add()
    template <typename Tag, typename Owner, typename... Args>
        requires(Tag::FromOwner && std::has_single_bit(Tag::Subtypes) && derivable_owner<Owner>)
    static typename Tag::Object* make_derived(Owner* owner, Args&&... args)
    {
        Q_ASSERT(owner);
        const object_id_value_t ownerNumber = TObjectIds::sequence_of(owner->object_id().to_storage());
        const std::optional<TObjectId<Tag>> id = TObjectId<Tag>::from_storage(owned_value<Tag>(ownerNumber));
        Q_ASSERT(id);
        return new typename Tag::Object(*id, owner, std::forward<Args>(args)...);
    }

    // Before a load: the counter above the saved next number and above every
    // number saved under root, so an object made during the load (its id
    // missing or damaged) never takes the number of an element further on. A
    // damaged next number (no number, or within the reserve) is ignored with a
    // warning; an id within the reserve is damaged and does not count
    void restore_sequence(const QDomElement& root, const QDomElement& properties)
    {
        bool isNumber = false;
        object_id_value_t savedNext = properties.attribute(NextObjectIdAttribute).toULongLong(&isNumber);
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

    // In get_state(): the object's own id and its owner's
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

    // A reference to another object: its id as bare hex. read_reference()
    // gives nothing when the attribute is missing, is no id, or is an id of
    // another kind; a prefix that names another kind than the number is
    // logged
    template <typename Tag>
    static void write_reference(QDomElement& element, const char* attribute, TObjectId<Tag> id)
    {
        element.setAttribute(attribute, QString::fromStdString(id.to_hex()));
    }
    template <typename Tag>
    static std::optional<TObjectId<Tag>> read_reference(const QDomElement& element, const char* attribute)
    {
        return reference_of<Tag>(element.attribute(attribute));
    }

    // A list of references in one attribute, ";"-joined. Entries that are no
    // id of the kind are left out
    template <typename Tag>
    static void write_references(QDomElement& element, const char* attribute, const QList<TObjectId<Tag>>& ids)
    {
        QStringList texts;
        for (const TObjectId<Tag>& id : ids) {
            texts.append(QString::fromStdString(id.to_hex()));
        }
        element.setAttribute(attribute, texts.join(';'));
    }
    template <typename Tag>
    static QList<TObjectId<Tag>> read_references(const QDomElement& element, const char* attribute)
    {
        QList<TObjectId<Tag>> ids;
        for (const QString& text : element.attribute(attribute).split(';', Qt::SkipEmptyParts)) {
            if (const std::optional<TObjectId<Tag>> id = reference_of<Tag>(text)) {
                ids.append(*id);
            } else {
                TLOG_W("ids", "the reference {} in the project file is no {} id: left out", text.toStdString(), Tag::Name);
            }
        }
        return ids;
    }

private:
    static constexpr const char* ObjectIdAttribute = "objectid";
    static constexpr const char* OwnerAttribute = "owner";
    static constexpr const char* NextObjectIdAttribute = "nextobjectid";
    static constexpr object_id_value_t SequenceMask = TObjectIds::SequenceMask;

    template <typename Tag>
    static constexpr object_id_value_t owned_value(object_id_value_t ownerNumber)
    {
        const uint8_t subtype = uint8_t(std::countr_zero(Tag::Subtypes));
        return (object_id_value_t(TObjectIds::make_tag(Tag::Kind, subtype)) << TObjectIds::SequenceBits) | ownerNumber;
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
        static_assert(Tag::InPool, "an object with a counter id is a peer: it is in the pool");
        T* object = construct<T>(id, std::forward<Args>(args)...);
        // One sequence for all kinds: no two objects share a number, so an id
        // derived from an owner's number is unique (a clip 7 and a track 7
        // never exist together)
        [[maybe_unused]] const bool added = m_pool.add(id, object);
        Q_ASSERT_X(added, "TObjectFactory::build", ("a number used twice: " + id.to_string()).c_str());
        return object;
    }

    static std::string text_of(const QDomElement& element, const char* attribute)
    {
        return element.attribute(attribute).toStdString();
    }

    // A bare hex id, or one with a prefix; 0 when it is no id
    static object_id_value_t raw_value(const QString& value)
    {
        bool ok = false;
        const object_id_value_t raw = value.mid(value.indexOf(':') + 1).toULongLong(&ok, 16);
        return ok ? raw : 0;
    }

    // The highest number of an objectid under element, ids within the reserve
    // left out
    static object_id_value_t highest_saved_number(const QDomElement& element)
    {
        const object_id_value_t number = TObjectIds::sequence_of(raw_value(element.attribute(ObjectIdAttribute)));
        object_id_value_t highest = number <= SequenceMask - SequenceReserve ? number : 0;
        for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
            highest = std::max(highest, highest_saved_number(child));
        }
        return highest;
    }

    // A bare hex id. A prefix ("audiotrack:", the form of the log and the
    // agent) is checked against the id's tag and then ignored. 0 when it is no id
    object_id_value_t read_value_text(const std::string& text)
    {
        const QString value = QString::fromStdString(text);
        const qsizetype colon = value.indexOf(':');
        const object_id_value_t raw = raw_value(value);
        if (raw == 0) {
            return 0;
        }
        if (colon >= 0 && value.left(colon) != QLatin1String(TObjectIds::name_of(TObjectIds::tag_of(raw)))) {
            TLOG_W("ids", "the id {} in the project file names another kind than its number has", text);
            ++m_warnings;
        }
        return raw;
    }

    object_id_value_t read_value(const QDomElement& element, const char* attribute)
    {
        return read_value_text(text_of(element, attribute));
    }

    template <typename Tag>
    static std::optional<TObjectId<Tag>> reference_of(const QString& text)
    {
        const qsizetype colon = text.indexOf(':');
        const object_id_value_t raw = raw_value(text);
        if (raw == 0) {
            return std::nullopt;
        }
        if (colon >= 0 && text.left(colon) != QLatin1String(TObjectIds::name_of(TObjectIds::tag_of(raw)))) {
            TLOG_W("ids", "the id {} in the project file names another kind than its number has", text.toStdString());
        }
        return TObjectId<Tag>::from_storage(raw);
    }

    template <typename Tag>
    std::optional<TObjectId<Tag>> read(const QDomElement& element, const char* attribute)
    {
        const object_id_value_t value = read_value(element, attribute);
        if (TObjectIds::sequence_of(value) > SequenceMask - SequenceReserve) {
            return std::nullopt;
        }
        return TObjectId<Tag>::from_storage(value);
    }

    TObjectIdGenerator& m_ids;
    TObjectPool& m_pool;
    int m_warnings{0};
};
