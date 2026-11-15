#include "liteorm/liteorm.hpp"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct Person {
    std::int64_t id;
    std::string label;
    std::int32_t age;
    bool active;
    liteorm::Optional<std::string> note;
    Person() : id(0), age(0), active(false) {}
};

struct People : liteorm::Table<Person> {
    liteorm::Column<Person, std::int64_t> id;
    liteorm::Column<Person, std::string> label;
    liteorm::Column<Person, std::int32_t> age;
    liteorm::Column<Person, bool> active;
    liteorm::Column<Person, liteorm::Optional<std::string> > note;
    explicit People(const std::string& name) : Table(name),
        id(add("id", &Person::id, liteorm::ColumnOptions::identity())),
        label(add("label", &Person::label, liteorm::ColumnOptions::unique_key())),
        age(add("age", &Person::age)),
        active(add("active", &Person::active)),
        note(add("note", &Person::note)) {}
};

template <typename Error, typename Func>
void expect_throw(Func action) {
    bool thrown = false;
    try { action(); }
    catch (const Error&) { thrown = true; }
    assert(thrown);
}

void check_cross_database(liteorm::Database& db, const std::string& suffix) {
    People people("liteorm_v07_portable_" + suffix);
    const std::string table = liteorm::quote_ident(people.name());
    db.create_table(people, false);
    try {
        Person first;
        first.label = "x' OR 1=1 --";
        first.age = 27;
        first.active = true;
        first.note = std::string(u8"한글 ✅");
        const std::int64_t a = db.insert(people, first);
        assert(a > 0);

        Person second;
        second.label = "";  // Empty TEXT must round-trip, not become SQL NULL.
        second.age = 19;
        const std::int64_t b = db.insert(people, second);
        assert(b > a);

        Person third;
        third.label = "last";
        third.age = 41;
        third.active = true;
        const std::int64_t c = db.insert(people, third);
        assert(c > b);

        assert(db.select(people).count() == 3);
        assert(db.select(people).where(people.label == first.label).count() == 1);
        assert(db.select(people).where(people.label == std::string("")).count() == 1);
        assert(db.select(people).where(people.note.is_null()).count() == 2);
        assert(db.select(people).where(people.note.is_not_null()).count() == 1);
        assert(db.select(people).where(people.age >= 20)
            .where(people.active == true).count() == 2);
        assert(db.select(people).where(! (people.age >= 20)).count() == 1);
        assert(db.select(people).where(people.label.in(std::vector<std::string>{
            first.label, third.label})).count() == 2);
        assert(db.select(people).where(people.label.in(std::vector<std::string>())).count() == 0);

        std::unique_ptr<Person> found = db.find(people, people.id, a);
        assert(found && found->label == first.label && found->note.has_value());
        assert(found->note.value() == std::string(u8"한글 ✅"));
        assert(db.find(people, people.id, b)->label == "");
        assert(!db.find(people, people.id, b)->note.has_value());
        assert(!db.find(people, people.id, static_cast<std::int64_t>(99999999)));

        // NULL must be bindable through Optional<T> on both databases.
        assert(db.update(people)
            .set(people.note, liteorm::Optional<std::string>())
            .where(people.id == a).execute() == 1);
        assert(db.select(people).where(people.note.is_null()).count() == 3);

        // Preserve existing documented text semantics: SQLite stores embedded
        // NUL bytes; PostgreSQL TEXT rejects them instead of truncating.
        Person binary_text;
        binary_text.label = std::string("abc\0def", 7);
        if (db.dialect() == liteorm::SqlDialect::SQLite) {
            const std::int64_t id = db.insert(people, binary_text);
            assert(db.find(people, people.id, id)->label == binary_text.label);
            assert(db.select(people).where(people.label == binary_text.label).count() == 1);
            assert(db.remove(people).where(people.id == id).execute() == 1);
        } else {
            expect_throw<std::invalid_argument>([&]() { db.insert(people, binary_text); });
            expect_throw<std::invalid_argument>([&]() {
                db.select(people).where(people.label == binary_text.label).all();
            });
        }
        assert(db.select(people).count() == 3);

        // A wrong dialect is rejected BEFORE driver execution.
        liteorm::SqlPlan wrong(db.dialect() == liteorm::SqlDialect::SQLite
            ? liteorm::SqlDialect::PostgreSQL : liteorm::SqlDialect::SQLite);
        wrong.sql = "DELETE FROM " + table;
        expect_throw<std::invalid_argument>([&]() { db.execute(wrong); });
        expect_throw<std::invalid_argument>([&]() { liteorm::Statement statement(db, wrong); });
        assert(db.select(people).count() == 3);

        // Roll back uncommitted changes even when a Transaction is moved.
        {
            liteorm::Transaction original = db.transaction();
            liteorm::Transaction moved = std::move(original);
            assert(db.remove(people).where(people.id == a).execute() == 1);
            expect_throw<std::logic_error>([&]() { original.commit(); });
            (void)moved; // destructor rolls back
        }
        assert(db.select(people).count() == 3);
        {
            liteorm::Transaction tx = db.transaction();
            assert(db.remove(people).where(people.id == c).execute() == 1);
            tx.commit();
            expect_throw<std::logic_error>([&]() { tx.commit(); });
        }
        assert(db.select(people).count() == 2);

        // Unique violation outside a transaction must not poison a connection.
        expect_throw<std::runtime_error>([&]() { db.insert(people, first); });
        assert(db.select(people).count() == 2);
        assert(db.select(people).order_by(people.id.asc()).limit(1).first()->id == a);
        assert(db.remove(people).all_rows().execute() == 2);
        assert(db.select(people).count() == 0);
    } catch (...) {
        db.execute_sql("DROP TABLE IF EXISTS " + table);
        throw;
    }
    db.execute_sql("DROP TABLE " + table);
}

int main(int argc, char** argv) {
    const bool use_pg = argc > 1 && std::string(argv[1]) == "--postgresql";
    const char* dsn = (std::getenv("LITEORM_TEST_PG_DSN") ? std::getenv("LITEORM_TEST_PG_DSN") : std::getenv("TINYORM_TEST_PG_DSN"));
    const char* strict = (std::getenv("LITEORM_REQUIRE_PG") ? std::getenv("LITEORM_REQUIRE_PG") : std::getenv("TINYORM_REQUIRE_PG"));
    if (use_pg && (!dsn || !*dsn)) {
        std::cerr << "PostgreSQL DSN unavailable\n";
        return strict && std::string(strict) == "1" ? 1 : 77;
    }
    try {
        assert(liteorm::ValueCodec<std::string>::encode(std::string("a\0b", 3)).text.size() == 3);
        assert(liteorm::quote_ident("a\"b") == "\"a\"\"b\"");
        struct Empty : liteorm::Table<Person> { Empty() : Table("empty") {} } empty;
        expect_throw<std::logic_error>([&]() {
            liteorm::insert_plan(empty, Person());
        });
        const std::string suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        if (use_pg) {
            liteorm::Database db(dsn, liteorm::BackendKind::PostgreSQL);
            check_cross_database(db, suffix);
        } else {
            liteorm::Database db(":memory:");
            check_cross_database(db, suffix);
        }
        std::cout << (use_pg ? "PostgreSQL" : "SQLite")
                  << " portable-core regression tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Portable-core test failed: " << e.what() << "\n";
        return 1;
    }
}
