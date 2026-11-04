// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TObjectFactory: new objects get new numbers in the order
// they are made, loaded ones the id of their element, a damaged or doubled id
// a new one, an owned object an id derived from its owner, owner references
// checked against the structure, and the sequence goes on above every number
// in the file from before the load on.

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
struct ItemTag { static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(1); static constexpr const char* Name = "item"; using Object = Item; };
struct PartTag { static constexpr bool FromOwner = true; static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(3); static constexpr const char* Name = "part"; using Object = Part; };
struct OtherPartTag { static constexpr bool FromOwner = true; static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(4); static constexpr const char* Name = "otherpart"; using Object = Part; };
// One class in two roles: a tag with a mask over both, as its member
struct PartsTag { static constexpr uint8_t Kind = 1; static constexpr uint64_t Subtypes = TObjectIds::subtype(3) | TObjectIds::subtype(4); static constexpr const char* Name = "parts"; using Object = Part; };
struct OwnerTag { static constexpr uint8_t Kind = 2; static constexpr uint64_t Subtypes = TObjectIds::subtype(1); static constexpr const char* Name = "owner"; using Object = Owner; };

// An object with a number; its owner is whatever the test hangs it on
class Item : public QObject
{
public:
    using IdTag = ItemTag;
    explicit Item(int value = 0) : value(value) {}
    uint64_t id() const { return m_objectId.to_storage(); }
    QObject* id_owner() const { return owner; }
    int value;
    QObject* owner{};

private:
    friend class TObjectFactory;
    TObjectId<ItemTag> m_objectId;
};

// A part in a fixed role of its owner, like a master; two roles, like
// fade-in and fade-out: one class, two subtypes
class Part : public QObject
{
public:
    using IdTag = void;
    explicit Part(QObject* owner) : owner(owner) {}
    uint64_t id() const { return m_objectId.to_storage(); }
    QObject* id_owner() const { return owner; }
    QObject* owner;

private:
    friend class TObjectFactory;
    TObjectId<PartsTag> m_objectId;
};

// An owner made with its id first, which makes its parts in its constructor
class Owner : public QObject
{
public:
    using IdTag = OwnerTag;
    Owner(TObjectId<OwnerTag> id, TObjectFactory& objects)
        : m_objectId(id)
    {
        const uint64_t number = TObjectIds::sequence_of(id.to_storage());
        part = objects.make_owned<Part, PartTag>(number, this);
        otherPart = objects.make_owned<Part, OtherPartTag>(number, this);
    }
    ~Owner() override
    {
        delete part;
        delete otherPart;
    }
    uint64_t id() const { return m_objectId.to_storage(); }
    QObject* id_owner() const { return nullptr; }
    Part* part{};
    Part* otherPart{};

private:
    friend class TObjectFactory;
    TObjectId<OwnerTag> m_objectId;
};

static_assert(objects_fit<PartsTag, PartTag, OtherPartTag>(), "both roles are a Part");

static uint64_t value(uint8_t subtype, uint64_t sequence)
{
    return (uint64_t(TObjectIds::make_tag(1, subtype)) << TObjectIds::SequenceBits) | sequence;
}

static QDomElement element_with(QDomDocument& doc, const QString& objectid, const QString& owner = QString())
{
    QDomElement e = doc.createElement("Item");
    doc.documentElement().appendChild(e);
    if (!objectid.isNull()) {
        e.setAttribute("objectid", objectid);
    }
    if (!owner.isNull()) {
        e.setAttribute("owner", owner);
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
    QObject project;
    QDomElement properties = doc.createElement("Properties");

    // New objects: numbers in the order they are made, enrolled, arguments passed on
    {
        TObjectIdGenerator ids;
        TObjectPool pool(&project);
        TObjectFactory objects(ids, pool);
        Item* a = objects.make<Item>(5);
        Item* b = objects.make<Item>();
        check(a->id() == value(1, 1) && b->id() == value(1, 2), "made objects get numbers 1 and 2");
        check(a->value == 5, "make() passes the constructor's arguments");
        check(pool.find_raw(a->id()) == a, "a made object is in the pool");

        QDomElement saved = doc.createElement("Item");
        TObjectFactory::write(saved, TObjectId<ItemTag>::from_storage(a->id()).value());
        TObjectFactory::write_owner(saved, TObjectId<ItemTag>::from_storage(b->id()).value());
        check(saved.attribute("objectid") == "0101000000000001" && saved.attribute("owner") == "0101000000000002",
              "write() and write_owner() give bare hex");
        delete a;
        delete b;
    }

    // Loading. The elements are made as they are loaded, so restore_sequence()
    // runs on a file with all of them first
    {
        TObjectIdGenerator ids;
        TObjectPool pool(&project);
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
                                                    (uint64_t(TObjectIds::make_tag(2, 1)) << TObjectIds::SequenceBits) | 7))),
                                                objects);
        check(TObjectIds::sequence_of(sameNumber->id()) == 15, "another kind with a number in use: a new one");

        check(ids.next() == 16, "after the load the counter is above every number");
        check(objects.check_owner_references() == 0, "elements without owner say nothing");
        check(objects.take_warning_count() == 6 && objects.take_warning_count() == 0, "six damaged ids, six warnings, counted once");
        delete sameNumber;

        for (Item* item : {loaded, prefixed, twice, wrongTag, missing, damaged, garbage}) {
            delete item;
        }
    }

    // Owner references: the structure wins, a dangling or different one warns
    {
        TObjectIdGenerator ids;
        TObjectPool pool(&project);
        TObjectFactory objects(ids, pool);
        Item* track = objects.load<Item>(element_with(doc, hex(value(1, 1))));
        Item* other = objects.load<Item>(element_with(doc, hex(value(1, 2))));
        Item* right = objects.load<Item>(element_with(doc, hex(value(1, 3)), hex(value(1, 1))));
        Item* dangling = objects.load<Item>(element_with(doc, hex(value(1, 4)), hex(value(1, 99))));
        Item* elsewhere = objects.load<Item>(element_with(doc, hex(value(1, 5)), hex(value(1, 2))));
        right->owner = track;
        dangling->owner = track;
        elsewhere->owner = track;
        check(objects.check_owner_references() == 2, "a dangling owner and another owner than the structure's: two warnings");
        check(pool.find_raw(dangling->id()) == dangling, "an object with a dangling owner stays");
        for (Item* item : {track, other, right, dangling, elsewhere}) {
            delete item;
        }
    }

    // Owned objects: the owner's number under the role's tag
    {
        TObjectIdGenerator ids;
        TObjectPool pool(&project);
        TObjectFactory objects(ids, pool);
        Item* counted = objects.make<Item>();
        Owner* first = objects.make<Owner>(objects);
        Owner* second = objects.make<Owner>(objects);
        const uint64_t firstNumber = TObjectIds::sequence_of(first->id());
        check(first->part->id() == value(3, firstNumber) && first->otherPart->id() == value(4, firstNumber),
              "two roles of one class: the owner's number under two subtypes");
        check(first->part->id() != second->part->id(), "two owners give their parts different ids");
        check(first->part->id() != counted->id() && second->otherPart->id() != counted->id(),
              "an owned id never meets one from the counter: its subtype is its own");
        check(pool.find_owner(first->part->id()) == first, "find_owner() gives the owner");
        check(pool.owners_out_of_place().isEmpty(), "the invariant holds");

        // The same steps again, as after a reload: the same derived ids
        TObjectIdGenerator ids2;
        TObjectPool pool2(&project);
        TObjectFactory objects2(ids2, pool2);
        delete objects2.make<Item>();
        Owner* again = objects2.make<Owner>(objects2);
        check(again->part->id() == first->part->id(), "the same steps give the same derived ids");
        delete again;

        // A saved derived id that does not follow from the owner: the owner wins
        QDomElement saved = element_with(doc, hex(value(3, 77)), hex(value(1, 77)));
        check(objects.check_owned<PartTag>(first->part, saved, first->id()) == 2, "a wrong saved id and owner: two warnings");
        check(first->part->id() == value(3, firstNumber), "the derived id stays");
        QDomElement right = element_with(doc, hex(first->part->id()), hex(first->id()));
        check(objects.check_owned<PartTag>(first->part, right, first->id()) == 0, "a right saved id and owner: no warning");

        // Hung on another owner by hand: the invariant finds it
        first->part->owner = second;
        check(pool.owners_out_of_place() == QList<uint64_t>{first->part->id()}, "a part hung on another owner is found");
        first->part->owner = first;

        Part* projectPart = objects.make_owned<Part, PartTag>(0, &project);
        check(projectPart->id() == value(3, 0) && pool.find_owner(projectPart->id()) == &project,
              "an owner without a number: number 0, owned by the root");
        delete projectPart;
        delete counted;
        delete first;
        delete second;
    }

    // A counter lowered by hand never hands out a number of the file, also not
    // to an object made before the element with that number is loaded
    {
        TObjectIdGenerator ids;
        TObjectPool pool(&project);
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
        Owner* fourth = objects.load<Owner>(fiftyOfAnOwner, objects);
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
            TObjectPool pool(&project);
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

    // The save writes the counter's next number
    {
        TObjectIdGenerator ids;
        TObjectPool pool(&project);
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
