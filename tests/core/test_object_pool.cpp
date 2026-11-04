// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TObjectPool: found by exact and wider ids, a duplicate
// refused, a destroyed object out of both indexes, derived ids outside the
// numbers, find_owner() and the owner invariant.

#include "TObjectPool.h"

#include <QCoreApplication>

#include <cstdio>
#include <memory>


static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

class Node : public QObject {};
class NodeA : public Node {};
class NodeB : public Node {};

struct NodeTag { static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::AllSubtypes; static constexpr const char* Name = "node"; using Object = Node; };
struct NodeATag { static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(1); static constexpr const char* Name = "nodea"; using Object = NodeA; };
struct NodeBTag { static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(2); static constexpr const char* Name = "nodeb"; using Object = NodeB; };

using NodeId = TObjectId<NodeTag>;
using NodeAId = TObjectId<NodeATag>;
using NodeBId = TObjectId<NodeBTag>;

static_assert(objects_fit<NodeTag, NodeATag, NodeBTag>(), "every node derives from Node");

template <typename Id, typename Object>
concept Adds = requires(TObjectPool& pool, Id id, Object* object) { pool.add(id, object); };
static_assert(Adds<NodeAId, NodeA>, "an exact id goes into the pool");
static_assert(!Adds<NodeId, Node>, "the id of the kind does not");

int main(int argc, char** argv)
{
    // The pool asserts it runs on the main thread
    QCoreApplication app(argc, argv);

    QObject root;
    TObjectIdGenerator ids;
    TObjectPool pool(&root);
    auto a = std::make_unique<NodeA>();
    NodeA owner;
    NodeB b;
    const NodeAId aId = NodeAId::create(ids);
    const NodeAId ownerId = NodeAId::create(ids);

    check(pool.add(aId, a.get()) && pool.add(ownerId, &owner), "two objects go in");
    check(!pool.add(aId, a.get()), "the same id a second time is refused");
    NodeB sameNumber;
    const uint64_t bWithANumber = (uint64_t(TObjectIds::make_tag(1, 2)) << TObjectIds::SequenceBits) | TObjectIds::sequence_of(aId.to_storage());
    check(!pool.add(NodeBId::from_storage(bWithANumber).value(), &sameNumber), "another tag with a number in use is refused");
    check(pool.find(aId) == a.get() && pool.find(NodeId(ownerId)) == &owner, "found by the exact id and by the id of the kind");
    check(pool.find(NodeAId()) == nullptr, "the invalid id finds nothing");
    check(pool.id_of(&owner) == ownerId.to_storage(), "id_of() gives the id of an object");

    // b derived from owner: its tag on the owner's number
    const uint64_t derived = (uint64_t(TObjectIds::make_tag(1, 2)) << TObjectIds::SequenceBits) | TObjectIds::sequence_of(ownerId.to_storage());
    QObject* bOwner = &owner;
    check(pool.add_owned(NodeBId::from_storage(derived).value(), &b, [&bOwner]() { return bOwner; }), "a derived object goes in");
    check(pool.find_owner(derived) == &owner, "find_owner() gives the owner from the number");
    check(pool.find_owner(TObjectIds::make_tag(1, 2) * (1ull << TObjectIds::SequenceBits)) == &root, "number 0 is the root's");
    check(pool.owners_out_of_place().isEmpty(), "the invariant holds");

    // b hung on another owner by hand: the invariant finds it
    bOwner = a.get();
    check(pool.owners_out_of_place() == QList<uint64_t>{derived}, "an object hung on another owner is found");
    bOwner = &owner;

    a.reset();
    check(!pool.contains(aId.to_storage()) && pool.find(aId) == nullptr, "a destroyed object leaves the pool");
    check(pool.id_of(nullptr) == 0, "no id for no object");
    check(pool.count() == 2, "no empty entry stays");

    pool.remove(ownerId);
    check(pool.find_owner(derived) == nullptr && pool.owners_out_of_place() == QList<uint64_t>{derived},
          "an owner removed: its derived object is out of place");

    if (failures > 0) {
        std::printf("%d object pool tests FAILED\n", failures);
        return 1;
    }
    std::printf("all object pool tests passed\n");
    return 0;
}
