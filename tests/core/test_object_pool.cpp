// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TObjectPool: found by exact and wider ids, a duplicate
// refused, a destroyed object out of both indexes, derived ids outside the
// numbers.

#include "TObjectIdTestAccess.h"
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
class NodeC;

struct NodeTag { static constexpr bool InPool = true; static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::AllSubtypes; static constexpr const char* Name = "node"; using Object = Node; };
struct NodeATag { static constexpr bool InPool = true; static constexpr bool FromOwner = false; static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(1); static constexpr const char* Name = "nodea"; using Object = NodeA; };
struct NodeBTag { static constexpr bool InPool = true; static constexpr bool FromOwner = false; static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(2); static constexpr const char* Name = "nodeb"; using Object = NodeB; };

// A peer derived from its owner, as a master
struct NodeCTag { static constexpr bool InPool = true; static constexpr bool FromOwner = true; static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(3); static constexpr const char* Name = "nodec"; using Object = NodeC; };
class NodeC : public Node
{
public:
    explicit NodeC(TObjectId<NodeCTag> id) : m_objectId(id) {}
    TObjectId<NodeCTag> object_id() const { return m_objectId; }

private:
    TObjectId<NodeCTag> m_objectId;
};

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

    TObjectIdGenerator ids;
    TObjectPool pool;
    auto a = std::make_unique<NodeA>();
    NodeA owner;
    const NodeAId aId = NodeAId::create(ids);
    const NodeAId ownerId = NodeAId::create(ids);

    check(pool.add(aId, a.get()) && pool.add(ownerId, &owner), "two objects go in");
    check(!pool.add(aId, a.get()), "the same id a second time is refused");
    NodeB sameNumber;
    const uint64_t bWithANumber = (uint64_t(TObjectIds::make_tag(1, 2)) << TObjectIds::SequenceBits) | TObjectIds::sequence_of(TObjectIdTestAccess::value(aId));
    check(!pool.add(TObjectIdTestAccess::from_value<NodeBId>(bWithANumber).value(), &sameNumber), "another tag with a number in use is refused");
    check(pool.find(aId) == a.get() && pool.find(NodeId(ownerId)) == &owner, "found by the exact id and by the id of the kind");
    check(pool.find(NodeAId()) == nullptr, "the invalid id finds nothing");

    // c derived from owner: its tag on the owner's number, enrolled with the id
    // it was made with
    const uint64_t derived = (uint64_t(TObjectIds::make_tag(1, 3)) << TObjectIds::SequenceBits) | TObjectIds::sequence_of(TObjectIdTestAccess::value(ownerId));
    NodeC b(TObjectIdTestAccess::from_value<TObjectId<NodeCTag>>(derived).value());
    check(pool.add<NodeCTag>(&b), "a derived peer goes in");
    check(pool.find_raw(derived) == &b, "a derived peer is found by its id");
    NodeC twice(b.object_id());
    check(!pool.add<NodeCTag>(&twice), "a derived id made twice is refused");

    a.reset();
    check(!pool.contains(TObjectIdTestAccess::value(aId)) && pool.find(aId) == nullptr, "a destroyed object leaves the pool");
    check(pool.count() == 2, "no empty entry stays");

    pool.remove(ownerId);
    check(pool.find_raw(derived) == &b, "an owner removed: its derived object stays until it leaves itself");

    if (failures > 0) {
        std::printf("%d object pool tests FAILED\n", failures);
        return 1;
    }
    std::printf("all object pool tests passed\n");
    return 0;
}
