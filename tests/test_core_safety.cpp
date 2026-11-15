#include "liteorm/liteorm.hpp"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

struct NumericRow {
    std::int64_t id;
    std::uint16_t small_unsigned;
    std::uint32_t large_unsigned;
    float ratio;
    bool enabled;
    liteorm::Optional<std::string> note;
    std::string label;
    NumericRow() : id(0), small_unsigned(0), large_unsigned(0), ratio(0), enabled(false) {}
};

struct NumericRows : liteorm::Table<NumericRow> {
    liteorm::Column<NumericRow, std::int64_t> id;
    liteorm::Column<NumericRow, std::uint16_t> small_unsigned;
    liteorm::Column<NumericRow, std::uint32_t> large_unsigned;
    liteorm::Column<NumericRow, float> ratio;
    liteorm::Column<NumericRow, bool> enabled;
    liteorm::Column<NumericRow, liteorm::Optional<std::string> > note;
    liteorm::Column<NumericRow, std::string> label;

    explicit NumericRows(const std::string& name) : Table(name),
        id(add("id", &NumericRow::id, liteorm::ColumnOptions::identity())),
        small_unsigned(add("small_unsigned", &NumericRow::small_unsigned)),
        large_unsigned(add("large_unsigned", &NumericRow::large_unsigned)),
        ratio(add("ratio", &NumericRow::ratio)),
        enabled(add("enabled", &NumericRow::enabled)),
        note(add("note", &NumericRow::note)),
        label(add("label", &NumericRow::label, liteorm::ColumnOptions::unique_key())) {}
};

template <typename Exception, typename F>
void must_throw(F fn) {
    bool thrown = false;
    try { fn(); } catch (const Exception&) { thrown = true; }
    if (!thrown) throw std::runtime_error("expected exception not raised");
}

void check_codec_and_sql_types() {
    assert(liteorm::ValueCodec<std::uint8_t>::pg_type() == std::string("SMALLINT"));
    assert(liteorm::ValueCodec<std::uint16_t>::pg_type() == std::string("INTEGER"));
    assert(liteorm::ValueCodec<std::uint32_t>::pg_type() == std::string("BIGINT"));
    assert(liteorm::ValueCodec<std::int16_t>::pg_type() == std::string("SMALLINT"));
    assert(liteorm::ValueCodec<std::int32_t>::pg_type() == std::string("INTEGER"));
    assert(liteorm::ValueCodec<std::int64_t>::pg_type() == std::string("BIGINT"));
    assert(liteorm::ValueCodec<std::uint32_t>::decode(
        liteorm::Value(static_cast<std::int64_t>(4294967295LL))) == 4294967295U);
    must_throw<std::out_of_range>([]() {
        liteorm::ValueCodec<std::uint64_t>::encode(std::numeric_limits<std::uint64_t>::max());
    });
    must_throw<std::out_of_range>([]() {
        liteorm::ValueCodec<std::int16_t>::decode(liteorm::Value(static_cast<std::int64_t>(40000)));
    });
    must_throw<std::out_of_range>([]() {
        liteorm::ValueCodec<float>::decode(liteorm::Value(1e100));
    });
    must_throw<std::runtime_error>([]() {
        liteorm::ValueCodec<bool>::decode(liteorm::Value(static_cast<std::int64_t>(2)));
    });
    assert(!liteorm::ValueCodec<liteorm::Optional<std::string> >::decode(
        liteorm::Value::null()).has_value());
}

void check_database(liteorm::Database& db, const std::string& suffix) {
    NumericRows rows("liteorm_safety_" + suffix);
    assert(rows.info(1).pg_type == "INTEGER");
    assert(rows.info(2).pg_type == "BIGINT");
    const std::string pg_ddl = liteorm::create_table_sql(rows, liteorm::SqlDialect::PostgreSQL);
    assert(pg_ddl.find("\"small_unsigned\" INTEGER") != std::string::npos);
    assert(pg_ddl.find("\"large_unsigned\" BIGINT") != std::string::npos);
    db.create_table(rows, false);

    try {
        NumericRow good;
        good.small_unsigned = 65535;
        good.large_unsigned = 4294967295U;
        good.ratio = 3.25f;
        good.enabled = true;
        good.label = "O'Reilly -- value is parameterized";
        const std::int64_t id = db.insert(rows, good);
        assert(id > 0);
        std::unique_ptr<NumericRow> loaded = db.find(rows, rows.id, id);
        assert(loaded.get() != NULL);
        assert(loaded->small_unsigned == 65535);
        assert(loaded->large_unsigned == 4294967295U);
        assert(loaded->ratio == 3.25f);
        assert(loaded->enabled);
        assert(!loaded->note.has_value());
        assert(loaded->label == good.label);

        // Both drivers must roll back when an error interrupts a transaction.
        // PostgreSQL enters an aborted state until ROLLBACK; RAII handles it.
        bool rejected = false;
        {
            liteorm::Transaction tx = db.transaction();
            NumericRow dup;
            dup.label = "transaction-duplicate";
            db.insert(rows, dup);
            try { db.insert(rows, dup); }
            catch (const liteorm::SqliteError& e) {
                assert(db.dialect() == liteorm::SqlDialect::SQLite);
                assert((e.code() & 0xff) == 19); // SQLITE_CONSTRAINT
                rejected = true;
            }
            catch (const liteorm::PostgresError& e) {
                assert(db.dialect() == liteorm::SqlDialect::PostgreSQL);
                assert(e.sqlstate() == "23505"); // unique_violation
                rejected = true;
            }
            assert(rejected);
            // No explicit commit: destructor rolls back on both backends.
        }
        assert(db.select(rows).count() == 1);
        assert(db.select(rows).where(rows.label == good.label).exists());
        assert(!db.select(rows).where(rows.label == "transaction-duplicate").exists());
        must_throw<std::logic_error>([&]() {
            db.update(rows).set(rows.enabled, false).execute();
        });
        must_throw<std::logic_error>([&]() {
            db.remove(rows).execute();
        });
        assert(db.select(rows).count() == 1);

        // A malformed statement should report an error without poisoning a
        // connection outside a transaction.
        liteorm::SqlPlan invalid(db.dialect());
        invalid.sql = "SELECT definitely_missing_column FROM " + liteorm::quote_ident(rows.name());
        must_throw<std::runtime_error>([&]() {
            liteorm::Statement stmt(db, invalid);
            stmt.step();
        });
        assert(db.select(rows).count() == 1);
        if (db.dialect() == liteorm::SqlDialect::SQLite) {
            // SELECT WHERE false must not return a stale sqlite3_changes() count.
            liteorm::SqlPlan readonly(db.dialect());
            readonly.sql = "SELECT 1 WHERE 0";
            must_throw<std::invalid_argument>([&]() { db.execute(readonly); });
        }
    } catch (...) {
        db.execute_sql("DROP TABLE IF EXISTS " + liteorm::quote_ident(rows.name()));
        throw;
    }
    db.execute_sql("DROP TABLE " + liteorm::quote_ident(rows.name()));
}

int main(int argc, char** argv) {
    const bool pg = argc > 1 && std::string(argv[1]) == "--postgresql";
    const char* dsn = (std::getenv("LITEORM_TEST_PG_DSN") ? std::getenv("LITEORM_TEST_PG_DSN") : std::getenv("TINYORM_TEST_PG_DSN"));
    const char* strict = (std::getenv("LITEORM_REQUIRE_PG") ? std::getenv("LITEORM_REQUIRE_PG") : std::getenv("TINYORM_REQUIRE_PG"));
    if (pg && (!dsn || !*dsn)) {
        std::cerr << "PostgreSQL DSN unavailable\n";
        return (strict && std::string(strict) == "1") ? 1 : 77;
    }
    try {
        check_codec_and_sql_types();
        const std::string suffix = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        if (pg) {
            liteorm::Database db(dsn, liteorm::BackendKind::PostgreSQL);
            check_database(db, suffix);
        } else {
            liteorm::Database db(":memory:");
            check_database(db, suffix);
        }
        std::cout << (pg ? "PostgreSQL" : "SQLite") << " safety regression tests passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Safety regression failure: " << e.what() << "\n";
        return 1;
    }
}
