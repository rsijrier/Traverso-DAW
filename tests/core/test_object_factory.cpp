// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TObjectFactory: new objects get new numbers in the order
// they are made, loaded ones the id of their element, a damaged or doubled id
// a new one, an owned object an id derived from its owner, the sequence goes
// on above every number in
// the file from before the load on, and references to other objects are
// written and read back.

#include "TObjectIdTestAccess.h"
#include "TObjectFactory.h"

#include <QCoreApplication>
#include <QDomDocument>

#include <cstdio>


static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

class Item;
class Part;
class Owner;
struct ItemTag { static constexpr bool InPool = true; static constexpr bool FromOwner = false; static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(1); static constexpr const char* Name = "item"; using Object = Item; };
struct PartTag { static constexpr bool InPool = false; static constexpr bool FromOwner = true; static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(3); static constexpr const char* Name = "part"; using Object = Part; };
struct OtherPartTag { static constexpr bool InPool = false; static constexpr bool FromOwner = true; static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(4); static constexpr const char* Name = "otherpart"; using Object = Part; };
// One class in two roles: a tag with a mask over both, as its member
struct PartsTag { static constexpr bool InPool = false; static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(3) | TObjectIds::subtype(4); static constexpr const char* Name = "parts"; using Object = Part; };
struct OwnerTag { static constexpr bool InPool = true; static constexpr bool FromOwner = false; static constexpr uint8_t Kind = 2; static constexpr uint64_t Subtypes = TObjectIds::subtype(1); static constexpr const char* Name = "owner"; using Object = Owner; };

// An object with a number
class Item : public QObject
{
public:
    using IdTag = ItemTag;
    explicit Item(int value = 0) : value(value) {}
    uint64_t id() const { return TObjectIdTestAccess::value(m_objectId); }
    int value;

private:
    friend class TObjectFactory;
    TObjectId<ItemTag> m_objectId;
};

// A part in a fixed role of its owner, like a fade; two roles, like fade-in
// and fade-out: one class, two subtypes. Not in the pool
class Part : public QObject
{
public:
    using IdTag = void;
    Part(TObjectId<PartsTag> id, QObject* owner) : owner(owner), m_objectId(id) {}
    uint64_t id() const { return TObjectIdTestAccess::value(m_objectId); }
    QObject* owner;

private:
    TObjectId<PartsTag> m_objectId;
};

// An owner made with its id first, which makes its parts in its constructor
class Owner : public QObject
{
public:
    using IdTag = OwnerTag;
    explicit Owner(TObjectId<OwnerTag> id)
        : m_objectId(id)
    {
        part = TObjectFactory::make_derived<PartTag>(this);
        otherPart = TObjectFactory::make_derived<OtherPartTag>(this);
    }
    ~Owner() override
    {
        delete part;
        delete otherPart;
    }
    uint64_t id() const { return TObjectIdTestAccess::value(m_objectId); }
    TObjectId<OwnerTag> object_id() const { return m_objectId; }
    Part* part{};
    Part* otherPart{};

private:
    friend class TObjectFactory;
    TObjectId<OwnerTag> m_objectId;
};

// The project: no number of its own (number 0)
class RootOwner : public QObject
{
public:
    using IdTag = OwnerTag;
    TObjectId<OwnerTag> object_id() const { return TObjectId<OwnerTag>::root(); }
};

static_assert(objects_fit<PartsTag, PartTag, OtherPartTag>(), "both roles are a Part");

// An owner whose own id is derived (as a master's): make_derived() refuses it,
// as its number is its owner's; its objects get a counter id
class DerivedOwner : public QObject
{
public:
    using IdTag = PartTag;
    TObjectId<PartTag> object_id() const { return {}; }
};
static_assert(derivable_owner<Owner>, "an owner with a counter id makes derived objects");
static_assert(derivable_owner<RootOwner>, "the project makes derived objects");
static_assert(!derivable_owner<DerivedOwner>, "an owner with a derived id does not");
static_assert(!derivable_owner<Part>, "a class made under several tags (IdTag void) does not");

// A part is not in the pool: add() and find() do not compile for its tag
template <typename Tag>
concept findable = requires(TObjectPool pool, TObjectId<Tag> id) { pool.find(id); };
template <typename Tag>
concept addable = requires(TObjectPool pool, TObjectId<Tag> id, typename Tag::Object* object) { pool.add(id, object); };
static_assert(findable<ItemTag> && addable<ItemTag>, "a peer is found and added by its id");
static_assert(!findable<PartTag> && !addable<PartTag>, "a part is not");

static uint64_t value(uint8_t subtype, uint64_t sequence)
{
    return (uint64_t(TObjectIds::make_tag(1, subtype)) << TObjectIds::SequenceBits) | sequence;
}

static QDomElement element_with(QDomDocument& doc, const QString& objectid)
{
    QDomElement e = doc.createElement("Item");
    doc.documentElement().appendChild(e);
    if (!objectid.isNull()) {
        e.setAttribute("objectid", objectid);
    }
    return e;
}

static QString hex(uint64_t raw)
{
    return QString::fromStdString(TObjectIds::to_hex(raw));
}

int main(int argc, char** argv)
{
    // The pool and the sequence assert they run on the main thread
    QCoreApplication app(argc, argv);
    QDomDocument doc;
    doc.appendChild(doc.createElement("Project"));
    QDomElement properties = doc.createElement("Properties");

    // New objects: numbers in the order they are made, enrolled, arguments passed on
    {
        TObjectIdGenerator ids;
        TObjectPool pool;
        TObjectFactory objects(ids, pool);
        Item* a = objects.make<Item>(5);
        Item* b = objects.make<Item>();
        check(a->id() == value(1, 1) && b->id() == value(1, 2), "made objects get numbers 1 and 2");
        check(a->value == 5, "make() passes the constructor's arguments");
        check(pool.find_raw(a->id()) == a, "a made object is in the pool");

        QDomElement saved = doc.createElement("Item");
        TObjectFactory::write(saved, TObjectIdTestAccess::from_value<TObjectId<ItemTag>>(a->id()).value());
        TObjectFactory::write_owner(saved, TObjectIdTestAccess::from_value<TObjectId<ItemTag>>(b->id()).value());
        check(saved.attribute("objectid") == "0101000000000001" && saved.attribute("owner") == "0101000000000002",
              "write() and write_owner() give bare hex");
        delete a;
        delete b;
    }

    // Loading. The elements are made as they are loaded, so restore_sequence()
    // runs on a file with all of them first
    {
        TObjectIdGenerator ids;
        TObjectPool pool;
        TObjectFactory objects(ids, pool);
        QDomDocument file;
        file.appendChild(file.createElement("Project"));
        for (const uint64_t raw : {value(1, 7), value(1, 8), value(2, 9), value(1, TObjectIds::SequenceMask - 5)}) {
            element_with(file, hex(raw));
        }
        properties.setAttribute("nextobjectid", "10");
        objects.restore_sequence(file.documentElement(), properties);
        check(ids.next() == 10, "before the load: the saved next number, above every number in the file but the damaged one");

        Item* loaded = objects.load<Item>(element_with(doc, hex(value(1, 7))));
        check(loaded->id() == value(1, 7), "a loaded object keeps the id of its element");
        Item* prefixed = objects.load<Item>(element_with(doc, "item:" + hex(value(1, 8))));
        check(prefixed->id() == value(1, 8), "a prefix is ignored");

        Item* twice = objects.load<Item>(element_with(doc, hex(value(1, 7))));
        check(twice->id() == value(1, 10), "an id used twice: a new one, from the saved counter on");
        Item* wrongTag = objects.load<Item>(element_with(doc, hex(value(2, 9))));
        check(wrongTag->id() == value(1, 11), "an id of another subtype: a new one");
        Item* missing = objects.load<Item>(element_with(doc, QString()));
        check(missing->id() == value(1, 12), "no id in the element: a new one");
        Item* damaged = objects.load<Item>(element_with(doc, hex(value(1, TObjectIds::SequenceMask - 5))));
        check(damaged->id() == value(1, 13), "an id near the end of the sequence counts as damaged");
        Item* garbage = objects.load<Item>(element_with(doc, "not hex"));
        check(garbage->id() == value(1, 14), "an id that is no number: a new one");
        Owner* sameNumber = objects.load<Owner>(element_with(doc, QString::fromStdString(TObjectIds::to_hex(
                                                    (uint64_t(TObjectIds::make_tag(2, 1)) << TObjectIds::SequenceBits) | 7))));
        check(TObjectIds::sequence_of(sameNumber->id()) == 15, "another kind with a number in use: a new one");

        check(ids.next() == 16, "after the load the counter is above every number");
        check(objects.take_warning_count() == 6 && objects.take_warning_count() == 0, "six damaged ids, six warnings, counted once");
        delete sameNumber;

        for (Item* item : {loaded, prefixed, twice, wrongTag, missing, damaged, garbage}) {
            delete item;
        }
    }

    // Owned objects: the owner's number under the role's tag
    {
        TObjectIdGenerator ids;
        TObjectPool pool;
        TObjectFactory objects(ids, pool);
        Item* counted = objects.make<Item>();
        Owner* first = objects.make<Owner>();
        Owner* second = objects.make<Owner>();
        const uint64_t firstNumber = TObjectIds::sequence_of(first->id());
        check(first->part->id() == value(3, firstNumber) && first->otherPart->id() == value(4, firstNumber),
              "two roles of one class: the owner's number under two subtypes");
        check(first->part->id() != second->part->id(), "two owners give their parts different ids");
        check(first->part->id() != counted->id() && second->otherPart->id() != counted->id(),
              "an owned id never meets one from the counter: its subtype is its own");
        check(first->part->owner == first, "make_derived() passes the owner on");

        // The same steps again, as after a reload: the same derived ids
        TObjectIdGenerator ids2;
        TObjectPool pool2;
        TObjectFactory objects2(ids2, pool2);
        delete objects2.make<Item>();
        Owner* again = objects2.make<Owner>();
        check(again->part->id() == first->part->id(), "the same steps give the same derived ids");
        delete again;

        RootOwner root;
        Part* projectPart = TObjectFactory::make_derived<PartTag>(&root);
        check(projectPart->id() == value(3, 0), "an owner without a number: number 0");
        check(pool.find_raw(first->part->id()) == nullptr && pool.find_raw(projectPart->id()) == nullptr,
              "a part is not in the pool");
        delete projectPart;
        delete counted;
        delete first;
        delete second;
    }

    // A counter lowered by hand never hands out a number of the file, also not
    // to an object made before the element with that number is loaded
    {
        TObjectIdGenerator ids;
        TObjectPool pool;
        TObjectFactory objects(ids, pool);
        QDomDocument file;
        file.appendChild(file.createElement("Project"));
        const QDomElement withoutId = element_with(file, QString());
        const QDomElement two = element_with(file, hex(value(1, 2)));
        const QDomElement fifty = element_with(file, hex(value(1, 50)));
        const QDomElement fiftyOfAnOwner = element_with(file, hex((uint64_t(TObjectIds::make_tag(2, 1)) << TObjectIds::SequenceBits) | 50));
        properties.setAttribute("nextobjectid", "2");
        objects.restore_sequence(file.documentElement(), properties);

        Item* first = objects.load<Item>(withoutId);
        Item* second = objects.load<Item>(two);
        Item* third = objects.load<Item>(fifty);
        Owner* fourth = objects.load<Owner>(fiftyOfAnOwner);
        check(first->id() == value(1, 51), "an element without an id: a number above every one in the file");
        check(second->id() == value(1, 2) && third->id() == value(1, 50), "the elements after it keep their ids");
        check(TObjectIds::sequence_of(fourth->id()) == 52, "a number in use under another kind: a new one, no assert");
        check(ids.next() == 53 && objects.take_warning_count() == 1, "the counter goes on above them; one warning");
        delete fourth;
        for (Item* item : {first, second, third}) {
            delete item;
        }
    }

    // A damaged next number is ignored with a warning; none is silent
    {
        QDomDocument file;
        file.appendChild(file.createElement("Project"));
        element_with(file, hex(value(1, 4)));
        for (const QString& next : {QStringLiteral("abc"), QString::number(TObjectIds::SequenceMask), QString()}) {
            TObjectIdGenerator ids;
            TObjectPool pool;
            TObjectFactory objects(ids, pool);
            QDomElement saved = doc.createElement("Properties");
            if (!next.isNull()) {
                saved.setAttribute("nextobjectid", next);
            }
            objects.restore_sequence(file.documentElement(), saved);
            check(ids.next() == 5 && objects.take_warning_count() == (next.isNull() ? 0 : 1),
                  next.isNull() ? "no next number: silent, above the file's numbers" : "a damaged next number: ignored, a warning");
        }
    }

    // References: written as bare hex, read back as the same id; a missing
    // attribute, no id or an id of another kind gives nothing
    {
        TObjectIdGenerator ids;
        TObjectPool pool;
        TObjectFactory objects(ids, pool);
        Item* a = objects.make<Item>();
        Item* b = objects.make<Item>();
        const TObjectId<ItemTag> aId = *TObjectIdTestAccess::from_value<TObjectId<ItemTag>>(a->id());
        const TObjectId<ItemTag> bId = *TObjectIdTestAccess::from_value<TObjectId<ItemTag>>(b->id());

        QDomElement e = doc.createElement("Reference");
        TObjectFactory::write_reference(e, "target", aId);
        check(e.attribute("target") == hex(a->id()), "write_reference() writes bare hex");
        check(TObjectFactory::read_reference<ItemTag>(e, "target") == aId, "read_reference() reads the same id");
        check(!TObjectFactory::read_reference<ItemTag>(e, "missing"), "a missing attribute: no id");
        e.setAttribute("junk", "no id");
        check(!TObjectFactory::read_reference<ItemTag>(e, "junk"), "a text that is no id: no id");
        e.setAttribute("other", hex(value(3, 1)));
        check(!TObjectFactory::read_reference<ItemTag>(e, "other"), "an id of another tag: no id");
        e.setAttribute("prefixed", "item:" + hex(a->id()));
        check(TObjectFactory::read_reference<ItemTag>(e, "prefixed") == aId, "a prefix of the right kind is read");

        TObjectFactory::write_references(e, "list", QList<TObjectId<ItemTag>>{aId, bId});
        check(e.attribute("list") == hex(a->id()) + ";" + hex(b->id()), "write_references() joins with ';'");
        check(TObjectFactory::read_references<ItemTag>(e, "list") == QList<TObjectId<ItemTag>>{aId, bId},
              "read_references() reads them back in order");
        e.setAttribute("mixed", hex(a->id()) + ";junk;" + hex(value(3, 1)) + ";" + hex(b->id()));
        check(TObjectFactory::read_references<ItemTag>(e, "mixed") == QList<TObjectId<ItemTag>>{aId, bId},
              "entries that are no id of the kind are left out");
        check(TObjectFactory::read_references<ItemTag>(e, "missing").isEmpty(), "a missing list is empty");
        delete a;
        delete b;
    }

    // The save writes the counter's next number
    {
        TObjectIdGenerator ids;
        TObjectPool pool;
        TObjectFactory objects(ids, pool);
        delete objects.make<Item>();
        QDomElement saved = doc.createElement("Properties");
        objects.write_sequence(saved);
        check(saved.attribute("nextobjectid") == "2", "write_sequence() writes the next number");
    }

    if (failures > 0) {
        std::printf("%d object factory tests FAILED\n", failures);
        return 1;
    }
    std::printf("all object factory tests passed\n");
    return 0;
}
