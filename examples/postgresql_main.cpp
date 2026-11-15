#include "liteorm/liteorm.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <cstdint>

struct Person { std::int64_t id; std::string name; int age; Person() : id(0), age(0) {} };
struct People : liteorm::Table<Person> {
    liteorm::Column<Person, std::int64_t> id;
    liteorm::Column<Person, std::string> name;
    liteorm::Column<Person, int> age;
    People() : Table("liteorm_example_people"),
        id(add("id", &Person::id, liteorm::ColumnOptions::identity())),
        name(add("name", &Person::name)), age(add("age", &Person::age)) {}
};
int main() {
    const char* dsn = std::getenv("LITEORM_PG_DSN");
    if (!dsn || !*dsn) { std::cerr << "Set LITEORM_PG_DSN first.\n"; return 2; }
    try {
        liteorm::Database db(dsn, liteorm::BackendKind::PostgreSQL);
        People people;
        db.create_table(people);
        Person user; user.name = "Ada"; user.age = 36;
        std::int64_t id = db.insert(people, user);
        const std::unique_ptr<Person> found = db.find(people, people.id, id);
        std::cout << "Inserted: " << found->id << " " << found->name << "\n";
        // This is a persistent example table: cleanup the inserted row.
        db.remove(people).where(people.id == id).execute();
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n"; return 1;
    }
    return 0;
}
