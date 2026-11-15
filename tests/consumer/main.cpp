#include "liteorm/liteorm.hpp"
#include <cassert>
#include <cstdint>
#include <string>
struct Record {
    std::int64_t id;
    std::string name;
    Record() : id(0) {}
};
struct Records : liteorm::Table<Record> {
    liteorm::Column<Record, std::int64_t> id;
    liteorm::Column<Record, std::string> name;
    Records() : Table("records"),
        id(add("id", &Record::id, liteorm::ColumnOptions::identity())),
        name(add("name", &Record::name)) {}
};
int main() {
    liteorm::Database db(":memory:");
    Records records;
    db.create_table(records);
    Record row;
    row.name = "installed consumer";
    const std::int64_t id = db.insert(records, row);
    assert(id > 0);
    assert(db.find(records, records.id, id)->name == row.name);
    assert(db.select(records).count() == 1);
}
