#include "liteorm/liteorm.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template <typename E, typename F>
void expect_throw(F f) {
    bool caught = false;
    try { f(); } catch (const E&) { caught = true; }
    assert(caught);
}
std::vector<liteorm::Migration> make_registry() {
    std::vector<liteorm::Migration> migrations;
    liteorm::Migration first("20261115_0001_accounts");
    first.up("CREATE TABLE migration_accounts (id INTEGER PRIMARY KEY, name TEXT NOT NULL)")
         .down("DROP TABLE migration_accounts");
    migrations.push_back(first);

    liteorm::Migration second("20261115_0002_audit");
    second.up("CREATE TABLE unreachable_common (id INTEGER)")
          .sqlite_up("CREATE TABLE migration_audit (id INTEGER PRIMARY KEY, account_id INTEGER REFERENCES migration_accounts(id))")
          .postgresql_up("CREATE TABLE migration_audit (id BIGINT PRIMARY KEY, account_id BIGINT REFERENCES migration_accounts(id))")
          .down("DROP TABLE migration_audit");
    migrations.push_back(second);
    return migrations;
}
bool exists(liteorm::Database& db, const std::string& name) {
    liteorm::SqlPlan plan(liteorm::SqlDialect::SQLite);
    plan.sql = "SELECT name FROM sqlite_master WHERE type='table' AND name = ";
    liteorm::append_parameter(plan, liteorm::Value(name));
    liteorm::Statement stmt(db, plan);
    return stmt.step();
}
struct Person { std::int64_t id; std::string name; Person() : id(0) {} };
struct People : liteorm::Table<Person> {
    liteorm::Column<Person,std::int64_t> id;
    liteorm::Column<Person,std::string> name;
    People() : Table("migration_people"),
        id(add("id", &Person::id, liteorm::ColumnOptions::identity())),
        name(add("name", &Person::name)) {}
};
} // namespace

int main() {
    using namespace liteorm;
    Database db(":memory:");
    MigrationRunner runner(db);
    std::vector<Migration> versions = make_registry();
    assert(runner.applied().empty());
    assert(runner.pending(versions).size() == 2);
    MigrationReport first = runner.apply_all(versions);
    assert(first.applied.size() == 2 && first.skipped.empty());
    assert(exists(db, "migration_accounts") && exists(db, "migration_audit"));
    assert(!exists(db, "unreachable_common"));
    const std::vector<MigrationRecord> history = runner.applied();
    assert(history.size() == 2);
    assert(history[0].version == "20261115_0001_accounts");
    assert(history[0].checksum == versions[0].checksum());
    assert(history[0].checksum.size() == 16);
    assert(!history[0].applied_at.empty());
    assert(runner.pending(versions).empty());
    MigrationReport again = runner.apply_all(versions);
    assert(again.applied.empty() && again.skipped.size() == 2);

    // Applied source cannot be removed or changed silently.
    expect_throw<std::logic_error>([&]() { runner.apply_all(std::vector<Migration>()); });
    std::vector<Migration> altered = versions;
    altered[0].up("CREATE TABLE additional_table (id INT)");
    expect_throw<std::logic_error>([&]() { runner.apply_all(altered); });
    assert(!exists(db, "additional_table"));

    // Registry validation rejects out of order, duplicates, bad identifiers, and empty SQL.
    expect_throw<std::invalid_argument>([&]() { Migration bad("has spaces"); });
    expect_throw<std::invalid_argument>([&]() { Migration bad(";DROP TABLE x"); });
    expect_throw<std::invalid_argument>([&]() { Migration bad("ok"); bad.up("   "); });
    expect_throw<std::invalid_argument>([&]() { Migration bad("ok"); bad.up(std::string("X\0Y",3)); });
    std::vector<Migration> wrong;
    wrong.push_back(versions[1]); wrong.push_back(versions[0]);
    expect_throw<std::invalid_argument>([&]() { runner.pending(wrong); });
    wrong.clear(); wrong.push_back(versions[0]); wrong.push_back(versions[0]);
    expect_throw<std::invalid_argument>([&]() { runner.apply_all(wrong); });

    // A failing migration rolls back its DDL, the history insert, and ALL prior migrations in the batch.
    Migration bad("20261115_0003_broken");
    bad.up("CREATE TABLE migration_temporary (id INTEGER)")
       .up("INTENTIONALLY INVALID SQL");
    versions.push_back(bad);
    expect_throw<SqliteError>([&]() { runner.apply_all(versions); });
    assert(!exists(db, "migration_temporary"));
    assert(runner.applied().size() == 2);
    versions.pop_back();

    // Fresh databases are all-or-nothing too: previous steps in the same batch
    // must not survive an error (including initial history table creation).
    {
        Database fresh(":memory:");
        MigrationRunner fresh_runner(fresh);
        std::vector<Migration> failing = make_registry();
        Migration bad_in_batch("20261115_0003_batch_failed");
        bad_in_batch.up("CREATE TABLE migration_rolled_back (id INTEGER)")
                    .up("BAD STATEMENT");
        failing.push_back(bad_in_batch);
        expect_throw<SqliteError>([&]() { fresh_runner.apply_all(failing); });
        assert(!exists(fresh, "migration_accounts"));
        assert(!exists(fresh, "migration_audit"));
        assert(!exists(fresh, "migration_rolled_back"));
        assert(fresh_runner.applied().empty());
    }

    // The public, non-executing DDL compiler is suitable for explicit per-dialect migrations.
    People people;
    assert(create_table_sql(people, SqlDialect::SQLite).find("AUTOINCREMENT") != std::string::npos);
    assert(create_table_sql(people, SqlDialect::PostgreSQL).find("BIGSERIAL") != std::string::npos);
    Migration from_metadata("20261115_0003_people");
    from_metadata.sqlite_up(create_table_sql(people, SqlDialect::SQLite))
                 .postgresql_up(create_table_sql(people, SqlDialect::PostgreSQL))
                 .down("DROP TABLE migration_people");
    versions.push_back(from_metadata);
    assert(runner.apply_all(versions).applied.size() == 1);
    assert(exists(db, "migration_people"));

    // Last-first rollback with explicit down SQL.
    assert(runner.rollback_last(versions));
    assert(!exists(db, "migration_people"));
    assert(runner.rollback_last(versions));
    assert(!exists(db, "migration_audit"));
    assert(runner.rollback_last(versions));
    assert(!exists(db, "migration_accounts"));
    assert(!runner.rollback_last(versions));
    assert(runner.applied().empty());

    // If down is absent, preserve history and database state.
    std::vector<Migration> irreversible;
    irreversible.push_back(Migration("0001_irreversible").up("CREATE TABLE irreversible (id INTEGER)"));
    assert(runner.apply_all(irreversible).applied.size() == 1);
    expect_throw<std::logic_error>([&]() { runner.rollback_last(irreversible); });
    assert(exists(db, "irreversible") && runner.applied().size() == 1);

    // An application-managed transaction cannot accidentally nest the migrator.
    { Transaction tx = db.transaction();
      expect_throw<std::logic_error>([&]() { runner.apply_all(irreversible); }); }
    std::cout << "SQLite migration integration tests passed\n";
}
