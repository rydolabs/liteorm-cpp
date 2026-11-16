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
    auto db = liteorm::Database::sqlite(":memory:");
    Records records;
    db.create_table(records);
    Record row;
    row.name = "installed consumer";
    const std::int64_t id = db.insert(records, row);
    assert(id > 0);
    assert(db.find(records, records.id, id)->name == row.name);
    assert(db.select(records).count() == 1);
    struct Item { std::int64_t id; std::string name; Item():id(0){} };
    db.model<Item>("installed_items").primary_key("id",&Item::id).field("name",&Item::name);
    auto items=db.repository<Item>();
    items.create_table();
    Item item;item.name="installed";items.create(item);
    assert(item.id>0 && items.find(item.id).has_value());
    assert(items.find_by(&Item::name,"installed").size()==1);
    assert(items.remove(item.id)==1);
}
