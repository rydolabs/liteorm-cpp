#include "liteorm/liteorm.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template<typename E, typename F> void expect_throw(F fn) {
    bool failed = false;
    try { fn(); } catch (const E&) { failed = true; }
    assert(failed);
}
std::int64_t count(liteorm::Database& db, const std::string& table) {
    liteorm::SqlPlan plan(liteorm::SqlDialect::PostgreSQL);
    plan.sql = "SELECT COUNT(*) FROM " + liteorm::quote_ident(table);
    liteorm::Statement stmt(db, plan);
    assert(stmt.step());
    std::int64_t n = liteorm::ValueCodec<std::int64_t>::decode(stmt.column(0));
    assert(!stmt.step());
    return n;
}
std::vector<liteorm::Migration> registry() {
    std::vector<liteorm::Migration> migrations;
    liteorm::Migration first("20261115_0001_accounts");
    first.sqlite_up("CREATE TABLE migration_accounts (id INTEGER PRIMARY KEY, name TEXT NOT NULL)")
         .postgresql_up("CREATE TABLE migration_accounts (id BIGINT PRIMARY KEY, name TEXT NOT NULL)")
         .down("DROP TABLE migration_accounts");
    migrations.push_back(first);
    liteorm::Migration second("20261115_0002_audit");
    second.up("CREATE TABLE migration_audit (id INTEGER PRIMARY KEY, notes TEXT)")
          .down("DROP TABLE migration_audit");
    migrations.push_back(second);
    return migrations;
}
}

int main() {
    const char* dsn = (std::getenv("LITEORM_TEST_PG_DSN") ? std::getenv("LITEORM_TEST_PG_DSN") : std::getenv("TINYORM_TEST_PG_DSN"));
    if (!dsn || !*dsn) {
        std::cout << "SKIP: Set LITEORM_TEST_PG_DSN for a writable PostgreSQL integration database.\n";
        const char* require_pg = (std::getenv("LITEORM_REQUIRE_PG") ? std::getenv("LITEORM_REQUIRE_PG") : std::getenv("TINYORM_REQUIRE_PG"));
        return (require_pg && std::string(require_pg) == "1") ? 1 : 77;
    }
    liteorm::Database db(dsn, liteorm::BackendKind::PostgreSQL);
    const std::string schema = "liteorm_mig_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    db.execute_sql("CREATE SCHEMA " + liteorm::quote_ident(schema));
    try {
        db.execute_sql("SET search_path TO " + liteorm::quote_ident(schema));
        liteorm::MigrationRunner runner(db);
        std::vector<liteorm::Migration> migrations = registry();
        assert(runner.pending(migrations).size() == 2);
        assert(runner.apply_all(migrations).applied.size() == 2);
        assert(runner.apply_all(migrations).skipped.size() == 2);
        assert(count(db, "_liteorm_migrations") == 2);
        assert(count(db, "migration_accounts") == 0);
        assert(runner.applied()[0].checksum == migrations[0].checksum());

        // An invalid PostgreSQL statement must roll back DDL and history changes.
        liteorm::Migration failed("20261115_0003_bad");
        failed.up("CREATE TABLE migration_will_rollback (id BIGINT)")
              .up("NOT A VALID POSTGRESQL STATEMENT");
        migrations.push_back(failed);
        expect_throw<liteorm::PostgresError>([&]() { runner.apply_all(migrations); });
        assert(count(db, "_liteorm_migrations") == 2);
        liteorm::SqlPlan check(liteorm::SqlDialect::PostgreSQL);
        check.sql = "SELECT to_regclass('migration_will_rollback') IS NULL";
        liteorm::Statement stmt(db, check);
        assert(stmt.step());
        assert(liteorm::ValueCodec<bool>::decode(stmt.column(0)));
        migrations.pop_back();

        std::vector<liteorm::Migration> modified = migrations;
        modified[1].up("CREATE TABLE unexpectedly_changed (id INTEGER)");
        expect_throw<std::logic_error>([&]() { runner.apply_all(modified); });
        assert(runner.rollback_last(migrations));
        assert(count(db, "_liteorm_migrations") == 1);
        assert(runner.rollback_last(migrations));
        assert(!runner.rollback_last(migrations));
        assert(count(db, "_liteorm_migrations") == 0);
        assert(runner.apply_all(migrations).applied.size() == 2);
    } catch (...) {
        db.execute_sql("SET search_path TO public");
        db.execute_sql("DROP SCHEMA " + liteorm::quote_ident(schema) + " CASCADE");
        throw;
    }
    db.execute_sql("SET search_path TO public");
    db.execute_sql("DROP SCHEMA " + liteorm::quote_ident(schema) + " CASCADE");
    std::cout << "PostgreSQL migration integration tests passed\n";
}
