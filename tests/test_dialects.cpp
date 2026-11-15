#include "liteorm/liteorm.hpp"
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <iostream>

struct Record {
    std::int64_t id;
    std::string name;
    int age;
    bool active;
    liteorm::Optional<std::string> note;
    Record() : id(0), age(0), active(false) {}
};
struct Records : liteorm::Table<Record> {
    liteorm::Column<Record, std::int64_t> id;
    liteorm::Column<Record, std::string> name;
    liteorm::Column<Record, int> age;
    liteorm::Column<Record, bool> active;
    liteorm::Column<Record, liteorm::Optional<std::string> > note;
    Records() : Table("records"),
        id(add("id", &Record::id, liteorm::ColumnOptions::identity())),
        name(add("name", &Record::name)),
        age(add("age", &Record::age)),
        active(add("active", &Record::active)),
        note(add("note", &Record::note)) {}
};

template <typename E, typename F>
void must_throw(F f) {
    bool thrown = false;
    try { f(); } catch (const E&) { thrown = true; }
    if (!thrown) throw std::runtime_error("expected exception");
}

int main() {
    Records t;
    const liteorm::SqlPlan sql = liteorm::select(t)
        .where((t.age >= 21 && t.name == std::string("X")) || t.note.is_null())
        .order_by(t.id.desc()).limit(5).compile(liteorm::SqlDialect::SQLite);
    const liteorm::SqlPlan pg = liteorm::select(t)
        .where((t.age >= 21 && t.name == std::string("X")) || t.note.is_null())
        .order_by(t.id.desc()).limit(5).compile(liteorm::SqlDialect::PostgreSQL);
    assert(sql.sql.find("\"records\".\"age\" >= ?") != std::string::npos);
    assert(sql.sql.find("\"records\".\"name\" = ?") != std::string::npos);
    assert(sql.sql.find(" LIMIT ?") != std::string::npos);
    assert(pg.sql.find("\"records\".\"age\" >= $1") != std::string::npos);
    assert(pg.sql.find("\"records\".\"name\" = $2") != std::string::npos);
    assert(pg.sql.find(" LIMIT $3") != std::string::npos);
    assert(pg.params.size() == 3);
    assert(pg.params[0].integer == 21 && pg.params[1].text == "X" && pg.params[2].integer == 5);
    assert(pg.dialect == liteorm::SqlDialect::PostgreSQL);
    assert(sql.dialect == liteorm::SqlDialect::SQLite);

    Record row;
    row.name = "O'Reilly";
    row.age = 32;
    row.active = true;
    const liteorm::SqlPlan pg_insert = liteorm::insert_plan(t, row, liteorm::SqlDialect::PostgreSQL);
    assert(pg_insert.sql == "INSERT INTO \"records\" (\"name\", \"age\", \"active\", \"note\") VALUES ($1, $2, $3, $4)");
    assert(pg_insert.params[0].text == "O'Reilly");
    assert(pg_insert.params[3].kind == liteorm::Value::Null);

    const liteorm::SqlPlan pg_update = liteorm::UpdateQuery<Record>(t)
        .set(t.age, 33).set(t.name, std::string("Bob"))
        .where(t.id == static_cast<std::int64_t>(12))
        .compile(liteorm::SqlDialect::PostgreSQL);
    assert(pg_update.sql == "UPDATE \"records\" SET \"age\" = $1, \"name\" = $2 WHERE \"records\".\"id\" = $3");
    assert(pg_update.params[2].integer == 12);

    const liteorm::SqlPlan pg_delete = liteorm::DeleteQuery<Record>(t)
        .where(t.active == true).compile(liteorm::SqlDialect::PostgreSQL);
    assert(pg_delete.sql == "DELETE FROM \"records\" WHERE \"records\".\"active\" = $1");
    assert(pg_delete.params[0].integer == 1);
    assert(t.info(0).pg_type == "BIGINT");
    assert(t.info(2).pg_type == "INTEGER");
    assert(t.info(3).pg_type == "BOOLEAN");
    assert(t.info(4).pg_type == "TEXT" && t.info(4).nullable);

    liteorm::Database db(":memory:");
    assert(db.backend_kind() == liteorm::BackendKind::SQLite);
    must_throw<std::invalid_argument>([&]() { db.execute(pg_delete); });
    must_throw<std::invalid_argument>([&]() { liteorm::Statement stmt(db, pg); });
    std::cout << "Dialect plan, binding order and mismatch tests passed.\n";
    return 0;
}
