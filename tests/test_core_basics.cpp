#include "liteorm/liteorm.hpp"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

struct Entry {
    std::int64_t id;
    std::string name;
    bool enabled;
    liteorm::Optional<std::string> memo;
    Entry() : id(0), enabled(false) {}
};

struct Entries : liteorm::Table<Entry> {
    liteorm::Column<Entry, std::int64_t> id;
    liteorm::Column<Entry, std::string> name;
    liteorm::Column<Entry, bool> enabled;
    liteorm::Column<Entry, liteorm::Optional<std::string> > memo;
    explicit Entries(const std::string& name) : Table(name),
        id(add("id", &Entry::id, liteorm::ColumnOptions::identity())),
        name(add("name", &Entry::name, liteorm::ColumnOptions::unique_key())),
        enabled(add("enabled", &Entry::enabled)),
        memo(add("memo", &Entry::memo)) {}
};

struct ManualKey {
    std::int64_t id;
    std::string name;
    ManualKey() : id(0) {}
};
struct ManualKeys : liteorm::Table<ManualKey> {
    liteorm::Column<ManualKey, std::int64_t> id;
    liteorm::Column<ManualKey, std::string> name;
    explicit ManualKeys(const std::string& table_name) : Table(table_name),
        id(add("id", &ManualKey::id, liteorm::ColumnOptions::primary())),
        name(add("name", &ManualKey::name)) {}
};

struct NoKey { std::string text; };
struct NoKeys : liteorm::Table<NoKey> {
    liteorm::Column<NoKey, std::string> text;
    explicit NoKeys(const std::string& name) : Table(name),
        text(add("text", &NoKey::text)) {}
};

struct StringKey { std::string id; };
struct StringKeys : liteorm::Table<StringKey> {
    liteorm::Column<StringKey, std::string> id;
    explicit StringKeys(const std::string& name) : Table(name),
        id(add("id", &StringKey::id, liteorm::ColumnOptions::primary())) {}
};

void check_basics(liteorm::Database& db, const std::string& suffix) {
    Entries entries("liteorm_basic_entries_" + suffix);
    ManualKeys manuals("liteorm_basic_manual_" + suffix);
    NoKeys no_keys("liteorm_basic_nokey_" + suffix);
    StringKeys strings("liteorm_basic_string_" + suffix);
    db.create_table(entries, false);
    db.create_table(manuals, false);
    db.create_table(no_keys, false);
    db.create_table(strings, false);

    // SQL generated for PostgreSQL without requiring a running server.
    const liteorm::SqlPlan pg_count = liteorm::select(entries)
        .where(entries.enabled == true)
        .order_by(entries.id.desc()).limit(0)
        .compile_count(liteorm::SqlDialect::PostgreSQL);
    assert(pg_count.sql.find("COUNT(*)") != std::string::npos);
    assert(pg_count.sql.find("$1") != std::string::npos);
    assert(pg_count.sql.find("ORDER BY") == std::string::npos);
    assert(pg_count.sql.find("LIMIT") == std::string::npos);
    assert(pg_count.params.size() == 1);
    const liteorm::SqlPlan pg_exists = liteorm::select(entries)
        .where(entries.enabled == false)
        .compile_exists(liteorm::SqlDialect::PostgreSQL);
    assert(pg_exists.sql.find("$1 LIMIT 1") != std::string::npos);
    assert(pg_exists.params.size() == 1);

    assert(db.select(entries).count() == 0);
    assert(!db.select(entries).exists());
    Entry first;
    first.name = "safe' OR 1=1 --";
    first.enabled = true;
    first.memo = std::string("nullable");
    std::int64_t first_id = db.insert(entries, first);
    assert(first_id > 0);
    Entry second;
    second.name = "second";
    const std::int64_t second_id = db.insert(entries, second);
    assert(second_id != first_id);

    assert(db.select(entries).count() == 2);
    assert(db.select(entries).where(entries.enabled == true).count() == 1);
    assert(db.select(entries).where(entries.name == first.name).count() == 1);
    assert(db.select(entries).where(entries.name == "missing").count() == 0);
    assert(db.select(entries).where(entries.memo.is_null()).count() == 1);
    assert(db.select(entries).where(entries.name == first.name).exists());
    assert(!db.select(entries).where(entries.name == "missing").exists());

    // Aggregate helpers count all matches and intentionally ignore the limit.
    assert(db.select(entries).limit(0).count() == 2);
    assert(db.select(entries).limit(0).exists());
    assert(db.select(entries).limit(0).all().empty());

    std::unique_ptr<Entry> found = db.find(entries, entries.id, first_id);
    assert(found && found->name == first.name && found->memo.has_value());
    assert(db.update(entries).set(entries.enabled, false)
               .where(entries.id == first_id).execute() == 1);
    assert(db.select(entries).where(entries.enabled == true).count() == 0);

    ManualKey manual;
    manual.id = 7001;
    manual.name = "manual-id";
    assert(db.insert(manuals, manual) == manual.id);
    assert(db.find(manuals, manuals.id, manual.id)->name == "manual-id");
    NoKey no_key;
    no_key.text = "no primary key";
    assert(db.insert(no_keys, no_key) == 0);
    StringKey string_key;
    string_key.id = "user-123";
    assert(db.insert(strings, string_key) == 0);
    assert(db.find(strings, strings.id, string_key.id)->id == "user-123");

    {
        liteorm::Transaction tx = db.transaction();
        db.remove(entries).where(entries.id == first_id).execute();
        // No commit -> rollback on scope exit.
    }
    assert(db.select(entries).count() == 2);
    {
        liteorm::Transaction tx = db.transaction();
        assert(db.remove(entries).where(entries.id == second_id).execute() == 1);
        tx.commit();
    }
    assert(db.select(entries).count() == 1);

    db.execute_sql("DROP TABLE " + liteorm::quote_ident(strings.name()));
    db.execute_sql("DROP TABLE " + liteorm::quote_ident(no_keys.name()));
    db.execute_sql("DROP TABLE " + liteorm::quote_ident(manuals.liteorm::Table<ManualKey>::name()));
    db.execute_sql("DROP TABLE " + liteorm::quote_ident(entries.liteorm::Table<Entry>::name()));
}

void check_file_persistence(const std::string& suffix) {
    const std::string file = "liteorm_sqlite_persist_" + suffix + ".db";
    Entries entries("persisted_entries");
    std::int64_t inserted = 0;
    {
        liteorm::Database db(file);
        db.create_table(entries);
        Entry person;
        person.name = "survives reconnect";
        inserted = db.insert(entries, person);
    }
    {
        liteorm::Database db(file);
        assert(db.select(entries).count() == 1);
        std::unique_ptr<Entry> row = db.find(entries, entries.id, inserted);
        assert(row && row->name == "survives reconnect");
    }
    if (std::remove(file.c_str()) != 0)
        throw std::runtime_error("unable to remove SQLite persistence test database");
}

int main(int argc, char** argv) {
    const bool pg = argc > 1 && std::string(argv[1]) == "--postgresql";
    const char* dsn = (std::getenv("LITEORM_TEST_PG_DSN") ? std::getenv("LITEORM_TEST_PG_DSN") : std::getenv("TINYORM_TEST_PG_DSN"));
    if (pg && (!dsn || !*dsn)) {
        std::cout << "SKIP: LITEORM_TEST_PG_DSN not set\n";
        const char* require_pg = (std::getenv("LITEORM_REQUIRE_PG") ? std::getenv("LITEORM_REQUIRE_PG") : std::getenv("TINYORM_REQUIRE_PG"));
        return (require_pg && std::string(require_pg) == "1") ? 1 : 77;
    }
    const std::string suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    if (pg) {
        liteorm::Database db(dsn, liteorm::BackendKind::PostgreSQL);
        check_basics(db, suffix);
    } else {
        liteorm::Database db(":memory:");
        check_basics(db, suffix);
        check_file_persistence(suffix);
    }
    std::cout << (pg ? "PostgreSQL" : "SQLite") << " core basics passed.\n";
    return 0;
}
