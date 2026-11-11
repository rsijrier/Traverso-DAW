// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Ids that cross a boundary: between domains, threads, the project file and the
// log (doc/design/object-ids.md). Support gives the mechanism only; which kinds
// exist and what they are called belongs to the domains (core: TObjectIdTags.h).
//
// An id is 64 bits: [8 kind | 8 subtype | 48 sequence]. The sequence is per
// project, from 1, in the order objects came into the project: unique within
// the project by construction, the same for the same steps. No clock.
//
// Kind 0 is no kind. A subtype is 0 to 63 (the top 2 bits of its byte are
// reserved) and never changes during an object's lifetime. The value 0 is the
// invalid id. Sequence 0 is used only by an id derived from an owner that has
// no number of its own (the project's master).

#include <QDataStream>
#include <QHashFunctions>

#include <bit>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>

// The value of an id as the project file and the pool's index hold it. Only
// they see it (TObjectFactory, TObjectPool): everyone else holds a TObjectId
using object_id_value_t = uint64_t;

template <typename Tag>
class TObjectId;

// The raw ids, for TObjectId and for whoever only has the number (the log, the
// project file)
struct TObjectIds
{
    static constexpr int SequenceBits = 48;
    static constexpr object_id_value_t SequenceMask = (object_id_value_t(1) << SequenceBits) - 1;
    static constexpr int SubtypeCount = 64;

    static constexpr uint16_t tag_of(object_id_value_t value) { return uint16_t(value >> SequenceBits); }
    static constexpr uint8_t kind_of(uint16_t tag) { return uint8_t(tag >> 8); }
    static constexpr uint8_t subtype_of(uint16_t tag) { return uint8_t(tag & 0xFF); }
    static constexpr uint16_t make_tag(uint8_t kind, uint8_t subtype) { return uint16_t((kind << 8) | subtype); }
    static constexpr object_id_value_t sequence_of(object_id_value_t value) { return value & SequenceMask; }

    // A mask of subtypes, for a tag struct's Subtypes
    static constexpr uint64_t AllSubtypes = ~0ull;
    static constexpr uint64_t subtype(uint8_t s) { return 1ull << s; }

    // An exact tag as the domain that owns it gives it at start-up: the name the
    // log and to_string() show (a literal: it lives as long as the program), and
    // whether its ids are derived from their owner (FromOwner)
    static void register_tag(uint16_t tag, const char* name, bool fromOwner);
    // The registered name, "id" when the tag has none
    static const char* name_of(uint16_t tag);
    // Whether the id is derived from its owner, by the tag in its value: such an
    // object writes no id and no owner. The tag must be registered
    template <typename Tag>
    static bool is_derived(TObjectId<Tag> id);
    // "audiotrack:0301000000000005": the tag's name and the whole value in hex.
    // Also the form for JSON and QML, which hold only 53 bits exact in a number
    static std::string to_string(object_id_value_t value);
    // "0301000000000005": the value alone, the form of the project file
    static std::string to_hex(object_id_value_t value);

private:
    static bool from_owner(uint16_t tag);
};

// The sequence of one project, owned by the project (no singleton). Ids are made
// in the GUI thread, never in the real-time path
class TObjectIdGenerator
{
public:
    // A new id with tag on top of the next sequence number; 0 (the invalid id)
    // when the sequence is used up, which asserts
    object_id_value_t create(uint16_t tag);

    // At load: the next number becomes the highest of what it is, the saved
    // next number and the highest loaded one + 1, so a hand-edited or missing
    // counter never hands out an existing id
    void restore(object_id_value_t savedNext, object_id_value_t highestLoaded);
    // For save
    object_id_value_t next() const;

private:
    mutable std::mutex m_mutex;
    object_id_value_t m_next{1};
};

// An id of one kind: Tag is a struct with
//   static constexpr uint8_t Kind;          1 to 255
//   static constexpr uint64_t Subtypes;     the subtypes it accepts, a mask
//   static constexpr const char* Name;      for the log
//   using Object = ...;                     the class find() gives back
// as the domains define them. One subtype in the mask: an exact id, the only
// kind create() makes. More: a layer (a mixer track is audio or bus). All: any
// object of the kind. An id converts implicitly to a tag whose set holds its own.
class TObjectPool;
class TObjectFactory;
// Defined by the tests only, which check the values
struct TObjectIdTestAccess;

template <typename Tag>
class TObjectId
{
    static_assert(Tag::Kind != 0, "kind 0 is no kind");
    static_assert(Tag::Subtypes != 0, "a tag accepts at least one subtype");

public:
    // The invalid id: 0, which create() never gives
    TObjectId() = default;

    // Only for an exact tag: an object is made with its subtype (AudioTrackId,
    // not TrackId)
    static TObjectId create(TObjectIdGenerator& generator)
        requires(std::has_single_bit(Tag::Subtypes))
    {
        const uint8_t subtype = uint8_t(std::countr_zero(Tag::Subtypes));
        return TObjectId(generator.create(TObjectIds::make_tag(Tag::Kind, subtype)));
    }

    // The id on number 0 of an exact tag: the project's own, which has no
    // number from the sequence
    static constexpr TObjectId root()
        requires(std::has_single_bit(Tag::Subtypes))
    {
        const uint8_t subtype = uint8_t(std::countr_zero(Tag::Subtypes));
        return TObjectId(object_id_value_t(TObjectIds::make_tag(Tag::Kind, subtype)) << TObjectIds::SequenceBits);
    }

    // AudioTrackId to AudioMixerTrackId to TrackId: always fits, so implicit
    template <typename Other>
        requires(!std::is_same_v<Other, Tag> && Other::Kind == Tag::Kind && (Other::Subtypes & ~Tag::Subtypes) == 0)
    TObjectId(TObjectId<Other> other)
        : m_value(other.m_value)
    {
    }

    uint16_t tag() const { return TObjectIds::tag_of(m_value); }
    // The number in the project's sequence, without the tag
    object_id_value_t sequence() const { return TObjectIds::sequence_of(m_value); }
    bool is_valid() const { return m_value != 0; }
    std::string to_string() const { return TObjectIds::to_string(m_value); }
    std::string to_hex() const { return TObjectIds::to_hex(m_value); }

    friend bool operator==(TObjectId, TObjectId) = default;

    // For QHash keys
    friend size_t qHash(TObjectId id, size_t seed = 0) noexcept { return qHash(id.m_value, seed); }

    // For item data that travels in a drag (QVariant): an id that does not
    // fit this tag reads as the invalid id
    friend QDataStream& operator<<(QDataStream& stream, TObjectId id) { return stream << quint64(id.m_value); }
    friend QDataStream& operator>>(QDataStream& stream, TObjectId& id)
    {
        quint64 value = 0;
        stream >> value;
        id = from_storage(value).value_or(TObjectId());
        return stream;
    }

private:
    template <typename>
    friend class TObjectId;
    friend class TObjectPool;
    friend class TObjectFactory;
    friend struct TObjectIdTestAccess;

    explicit constexpr TObjectId(object_id_value_t value)
        : m_value(value)
    {
    }

    // Does a value fit: same kind, its subtype in the mask
    static constexpr bool accepts(object_id_value_t value)
    {
        const uint16_t tag = TObjectIds::tag_of(value);
        const uint8_t subtype = TObjectIds::subtype_of(tag);
        return value != 0 && TObjectIds::kind_of(tag) == Tag::Kind && subtype < TObjectIds::SubtypeCount
               && ((Tag::Subtypes >> subtype) & 1u);
    }

    // An id from the project file or the index, when it fits this tag
    static std::optional<TObjectId> from_storage(object_id_value_t value)
    {
        if (!accepts(value)) {
            return std::nullopt;
        }
        return TObjectId(value);
    }

    object_id_value_t to_storage() const { return m_value; }

    object_id_value_t m_value{0};
};

// True when the Object of every exact tag in Exact whose subtype Set accepts
// derives from Set's Object: then find() on a Set id may cast to it. For a
// static_assert where all the classes are defined; the compiler cannot see it
// from the tags alone
template <typename Set, typename... Exact>
constexpr bool objects_fit()
{
    static_assert((std::has_single_bit(Exact::Subtypes) && ...), "objects_fit() takes exact tags after the set");
    return ((Exact::Kind != Set::Kind || (Exact::Subtypes & Set::Subtypes) == 0
             || std::is_base_of_v<typename Set::Object, typename Exact::Object>) && ...);
}

template <typename Tag>
bool TObjectIds::is_derived(TObjectId<Tag> id)
{
    return from_owner(id.tag());
}

template <typename Tag>
struct std::hash<TObjectId<Tag>>
{
    size_t operator()(TObjectId<Tag> id) const noexcept { return qHash(id); }
};
