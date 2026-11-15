#include "liteorm/liteorm.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

struct Item {
    std::int64_t id;
    std::string label;
    int quantity;
    bool active;
    double price;
    liteorm::Optional<std::string> note;
    Item() : id(0), quantity(0), active(false), price(0) {}
};
struct ItemTable : liteorm::Table<Item> {
    liteorm::Column<Item, std::int64_t> id;
    liteorm::Column<Item, std::string> label;
    liteorm::Column<Item, int> quantity;
    liteorm::Column<Item, bool> active;
    liteorm::Column<Item, double> price;
    liteorm::Column<Item, liteorm::Optional<std::string> > note;
    explicit ItemTable(const std::string& name) : Table(name),
        id(add("id", &Item::id, liteorm::ColumnOptions::identity())),
        label(add("label", &Item::label, liteorm::ColumnOptions::unique_key())),
        quantity(add("quantity", &Item::quantity)),
        active(add("active", &Item::active)),
        price(add("price", &Item::price)),
        note(add("note", &Item::note)) {}
};

struct ItemTag {
    std::int64_t id;
    std::int64_t item_id;
    std::string tag;
    ItemTag() : id(0), item_id(0) {}
};
struct ItemTagTable : liteorm::Table<ItemTag> {
    liteorm::Column<ItemTag, std::int64_t> id;
    liteorm::Column<ItemTag, std::int64_t> item_id;
    liteorm::Column<ItemTag, std::string> tag;
    explicit ItemTagTable(const std::string& n, const std::string& parent) : Table(n),
        id(add("id", &ItemTag::id, liteorm::ColumnOptions::identity())),
        item_id(add("item_id", &ItemTag::item_id, liteorm::ColumnOptions::foreign_key(parent, "id"))),
        tag(add("tag", &ItemTag::tag)) {}
};

struct ItemTagFlag {
    std::int64_t id;
    std::int64_t tag_id;
    std::string label;
    ItemTagFlag() : id(0), tag_id(0) {}
};
struct ItemTagFlagTable : liteorm::Table<ItemTagFlag> {
    liteorm::Column<ItemTagFlag, std::int64_t> id;
    liteorm::Column<ItemTagFlag, std::int64_t> tag_id;
    liteorm::Column<ItemTagFlag, std::string> label;
    explicit ItemTagFlagTable(const std::string& n, const std::string& parent) : Table(n),
        id(add("id", &ItemTagFlag::id, liteorm::ColumnOptions::identity())),
        tag_id(add("tag_id", &ItemTagFlag::tag_id,
            liteorm::ColumnOptions::foreign_key(parent, "id"))),
        label(add("label", &ItemTagFlag::label)) {}
};

template <typename E, typename F>
void must_throw(F f) {
    bool caught = false;
    try { f(); } catch (const E&) { caught = true; }
    if (!caught) throw std::runtime_error("expected exception was not thrown");
}

int main() {
    const char* dsn = (std::getenv("LITEORM_TEST_PG_DSN") ? std::getenv("LITEORM_TEST_PG_DSN") : std::getenv("TINYORM_TEST_PG_DSN"));
    if (!dsn || !*dsn) {
        std::cout << "SKIP: Set LITEORM_TEST_PG_DSN to a writable PostgreSQL test database.\n";
        const char* require_pg = (std::getenv("LITEORM_REQUIRE_PG") ? std::getenv("LITEORM_REQUIRE_PG") : std::getenv("TINYORM_REQUIRE_PG"));
        return (require_pg && std::string(require_pg) == "1") ? 1 : 77;
    }
    try {
        liteorm::Database db(dsn, liteorm::BackendKind::PostgreSQL);
        assert(db.dialect() == liteorm::SqlDialect::PostgreSQL);
        const std::string name = "liteorm_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        ItemTable items(name);
        ItemTagTable tags(name + "_tags", name);
        ItemTagFlagTable flags(name + "_flags", tags.name());
        db.create_table(items, false);
        db.create_table(tags, false);
        db.create_table(flags, false);
        try {
            Item alice;
            alice.label = "Alice"; alice.quantity = 42; alice.active = true;
            alice.price = 9.125; alice.note = std::string("note");
            const std::int64_t a_id = db.insert(items, alice);
            assert(a_id > 0);
            Item second;
            second.label = "SQL '); DROP TABLE x; --";
            second.quantity = -1; second.price = -0.25;
            const std::int64_t b_id = db.insert(items, second);
            assert(b_id == a_id + 1);

            ItemTag t; t.item_id = a_id; t.tag = "favorite";
            const std::int64_t tag_id = db.insert(tags, t);
            ItemTagFlag flag; flag.tag_id = tag_id; flag.label = "checked";
            db.insert(flags, flag);
            ItemTag orphan; orphan.item_id = static_cast<std::int64_t>(-12345);
            orphan.tag = "orphan";
            must_throw<liteorm::PostgresError>([&]() { db.insert(tags, orphan); });
            const liteorm::SqlPlan joined = db.join(items, tags)
                .on(items.id == tags.item_id)
                .where(tags.tag == std::string("favorite"))
                .limit(5).compile();
            assert(joined.sql.find(" INNER JOIN ") != std::string::npos);
            assert(joined.sql.find(" LIMIT $2") != std::string::npos);
            const std::vector<std::pair<Item, ItemTag> > join_rows = db.join(items, tags)
                .on(items.id == tags.item_id).where(items.id == a_id).all();
            assert(join_rows.size() == 1);
            assert(join_rows[0].first.label == "Alice" &&
                   join_rows[0].second.tag == "favorite");
            // PostgreSQL live LEFT JOIN, triple JOIN and relation loading.
            const std::vector<std::pair<Item,liteorm::Optional<ItemTag> > > left_rows =
                db.left_join(items, tags).on(items.id == tags.item_id)
                    .order_by(items.id.asc()).all();
            assert(left_rows.size() == 2);
            assert(left_rows[0].second.has_value() && left_rows[0].first.id == a_id);
            assert(!left_rows[1].second.has_value() && left_rows[1].first.id == b_id);
            const std::vector<std::tuple<Item,ItemTag,ItemTagFlag> > triples =
                db.join(items, tags).on(items.id == tags.item_id)
                  .join(flags).on(tags.id == flags.tag_id).all();
            assert(triples.size() == 1);
            assert(std::get<2>(triples[0]).label == "checked");
            const liteorm::HasMany<Item,ItemTag,std::int64_t> has_tags =
                liteorm::has_many(items, items.id, tags, tags.item_id);
            const std::vector<Item> parents = db.select(items).order_by(items.id.asc()).all();
            const std::vector<std::vector<ItemTag> > grouped = db.load_many(has_tags, parents);
            assert(grouped.size() == 2 && grouped[0].size() == 1 && grouped[1].empty());
            const liteorm::BelongsTo<ItemTag,Item,std::int64_t> belongs =
                liteorm::belongs_to(tags, tags.item_id, items, items.id);
            assert(db.load(belongs, t)->label == "Alice");
            std::vector<std::int64_t> lookup_ids;
            lookup_ids.push_back(a_id);
            lookup_ids.push_back(b_id);
            assert(db.select(items).where(items.id.in(lookup_ids)).all().size() == 2);

            std::unique_ptr<Item> a = db.find(items, items.id, a_id);
            assert(a && a->label == "Alice" && a->quantity == 42 && a->active);
            assert(a->price == 9.125 && a->note.has_value() && a->note.value() == "note");
            std::unique_ptr<Item> b = db.find(items, items.id, b_id);
            assert(b && b->label == second.label && b->quantity == -1 && !b->active);
            assert(!b->note.has_value() && b->price == -0.25);
            assert(db.select(items).where(items.quantity >= 0)
                .order_by(items.id.asc()).limit(1).all().size() == 1);
            assert(db.select(items).where(items.note.is_null()).all().size() == 1);
            assert(db.select(items).where(items.active == true).all().size() == 1);
            assert(db.select(items).where(items.label == second.label).all().size() == 1);

            assert(db.update(items).set(items.quantity, 45)
                .where(items.id == a_id).execute() == 1);
            assert(db.find(items, items.id, a_id)->quantity == 45);
            assert(db.update(items).set(items.quantity, 1)
                .where(items.id == static_cast<std::int64_t>(-999)).execute() == 0);
            must_throw<liteorm::PostgresError>([&]() { db.insert(items, alice); });

            { liteorm::Transaction tx = db.transaction();
              // Drop dependent rows inside the rollback-only transaction.
              db.remove(flags).all_rows().execute();
              db.remove(tags).all_rows().execute();
              db.remove(items).where(items.id == a_id).execute();
              assert(!db.find(items, items.id, a_id));
              must_throw<std::logic_error>([&]() { db.transaction(); }); }
            assert(db.find(items, items.id, a_id));
            { liteorm::Transaction tx = db.transaction();
              db.remove(items).where(items.id == b_id).execute();
              tx.rollback(); }
            assert(db.find(items, items.id, b_id));
            { liteorm::Transaction tx = db.transaction();
              db.remove(items).where(items.id == b_id).execute();
              tx.commit(); }
            assert(!db.find(items, items.id, b_id));
            must_throw<std::invalid_argument>([&]() {
                Item invalid;
                invalid.label = std::string("A\0B", 3);
                db.insert(items, invalid);
            });
            assert(db.remove(flags).all_rows().execute() == 1);
            assert(db.remove(tags).all_rows().execute() == 1);
            assert(db.remove(items).all_rows().execute() == 1);
            assert(db.select(items).all().empty());
        } catch (...) {
            db.execute_sql("DROP TABLE IF EXISTS " + liteorm::quote_ident(flags.name()));
            db.execute_sql("DROP TABLE IF EXISTS " + liteorm::quote_ident(tags.name()));
            db.execute_sql("DROP TABLE IF EXISTS " + liteorm::quote_ident(items.name()));
            throw;
        }
        db.execute_sql("DROP TABLE IF EXISTS " + liteorm::quote_ident(flags.name()));
        db.execute_sql("DROP TABLE IF EXISTS " + liteorm::quote_ident(tags.name()));
        db.execute_sql("DROP TABLE IF EXISTS " + liteorm::quote_ident(items.name()));
        std::cout << "PostgreSQL live C++11 ORM integration tests passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "PostgreSQL test failed: " << e.what() << "\n";
        return 1;
    }
}
