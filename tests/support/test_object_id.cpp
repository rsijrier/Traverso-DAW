// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TObjectId and TObjectIdGenerator: which ids convert into
// which (by the compiler), what a raw value is accepted as, and that a
// project's sequence gives the same ids for the same steps.

#include "TObjectId.h"

#include <QCoreApplication>

#include <cstdio>
#include <unordered_set>


static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

struct Base {};
struct DerivedA : Base {};
struct DerivedB : Base {};
struct Unrelated {};

// Kind 1 with an any tag, a layer of subtypes 1 and 2, and three exact tags;
// kind 2 with one exact tag
struct AnyTag { static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::AllSubtypes; static constexpr const char* Name = "any"; using Object = Base; };
struct LayerTag { static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(1) | TObjectIds::subtype(2); static constexpr const char* Name = "layer"; using Object = Base; };
struct ATag { static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(1); static constexpr const char* Name = "a"; using Object = DerivedA; };
struct BTag { static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(2); static constexpr const char* Name = "b"; using Object = DerivedB; };
struct CTag { static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(3); static constexpr const char* Name = "c"; using Object = Unrelated; };
struct OtherKindTag { static constexpr uint8_t Kind = 2; static constexpr uint64_t Subtypes = TObjectIds::subtype(1); static constexpr const char* Name = "other"; using Object = Unrelated; };

using AnyId = TObjectId<AnyTag>;
using LayerId = TObjectId<LayerTag>;
using AId = TObjectId<ATag>;
using BId = TObjectId<BTag>;
using CId = TObjectId<CTag>;
using OtherId = TObjectId<OtherKindTag>;

// Conversions: into a tag whose set holds the own one, never out of it
static_assert(std::is_convertible_v<AId, LayerId>, "an exact id fits its layer");
static_assert(std::is_convertible_v<AId, AnyId>, "an exact id fits the kind");
static_assert(std::is_convertible_v<LayerId, AnyId>, "a layer fits the kind");
static_assert(!std::is_convertible_v<LayerId, AId>, "a layer is not an exact id");
static_assert(!std::is_convertible_v<AnyId, LayerId>, "the kind is not a layer");
static_assert(!std::is_convertible_v<CId, LayerId>, "an exact id outside the layer does not fit it");
static_assert(!std::is_convertible_v<AId, BId>, "one exact id is never another");
static_assert(!std::is_convertible_v<AId, OtherId>, "an id of one kind is never one of another");

// Only exact tags make ids
template <typename Id>
concept Creates = requires(TObjectIdGenerator& generator) { Id::create(generator); };
static_assert(Creates<AId>, "an exact tag creates");
static_assert(!Creates<LayerId>, "a layer does not create");
static_assert(!Creates<AnyId>, "the kind does not create");

// find() may cast a layer's or the kind's id to Base: every exact Object in it
// derives from Base. C's Object does not, and the kind accepts C
static_assert(objects_fit<LayerTag, ATag, BTag, CTag, OtherKindTag>(), "the layer's objects derive from Base");
static_assert(!objects_fit<AnyTag, ATag, BTag, CTag>(), "C's object does not derive from Base");

int main(int argc, char** argv)
{
    // The generator asserts it runs on the main thread
    QCoreApplication app(argc, argv);

    TObjectIdGenerator ids;
    const AId a = AId::create(ids);
    const BId b = BId::create(ids);
    const OtherId other = OtherId::create(ids);

    check(!AId().is_valid() && AId().to_storage() == 0, "the default id is invalid and 0");
    check(a.to_storage() == 0x0101000000000001ull, "the first id: kind 1, subtype 1, number 1");
    check(b.to_storage() == 0x0102000000000002ull, "the second: kind 1, subtype 2, number 2");
    check(other.to_storage() == 0x0201000000000003ull, "the third, of another kind: number 3");

    // The same steps in another project give the same ids
    TObjectIdGenerator again;
    const AId a2 = AId::create(again);
    const BId b2 = BId::create(again);
    check(a2 == a && b2 == b, "the same steps give the same ids");

    const uint64_t raw = a.to_storage();
    check(AId::accepts(raw) && LayerId::accepts(raw) && AnyId::accepts(raw), "a's value fits a, the layer and the kind");
    check(!BId::accepts(raw) && !CId::accepts(raw) && !OtherId::accepts(raw), "a's value fits no other exact tag and no other kind");
    check(!AnyId::accepts(0), "0 is no id of any tag");
    check(AId::accepts(0x0101000000000000ull), "a tag on number 0 is an id: one derived from an owner without a number");
    check(!AnyId::accepts(0x0140000000000001ull), "subtype 64 (a reserved bit) is refused");
    check(!AnyId::accepts(other.to_storage()), "an id of another kind is refused by the kind");
    check(!LayerId::accepts(CId::create(ids).to_storage()), "an exact id outside the layer is refused by it");

    const std::optional<AId> back = AId::from_storage(raw);
    check(back.has_value() && *back == a, "from_storage() gives the same id back");
    check(!BId::from_storage(raw).has_value(), "from_storage() refuses a value of another tag");

    const LayerId layer = a;
    const AnyId any = layer;
    check(layer.to_storage() == raw && any.to_storage() == raw, "conversions keep the value");
    check(any == AnyId(a), "ids compare equal after a conversion");

    std::unordered_set<AId> hashed{a, a, AId::create(ids)};
    check(hashed.size() == 2, "ids hash as their value");

    check(a.to_string() == "id:0101000000000001", "an id without a registered name shows as id: and its hex");
    TObjectIds::register_name(a.tag(), ATag::Name);
    check(a.to_string() == "a:0101000000000001", "a registered name and the value in 16 hex digits");

    uint64_t previous = 0;
    bool increasing = true;
    for (int i = 0; i < 10000; ++i) {
        const uint64_t next = TObjectIds::sequence_of(AId::create(ids).to_storage());
        increasing = increasing && next > previous;
        previous = next;
    }
    check(increasing, "the numbers increase in creation order");

    if (failures > 0) {
        std::printf("%d object id tests FAILED\n", failures);
        return 1;
    }
    std::printf("all object id tests passed\n");
    return 0;
}
