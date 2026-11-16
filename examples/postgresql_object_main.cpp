#include "liteorm/liteorm.hpp"
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

struct User {
    std::int64_t id;
    std::string name;
    int age;
    User() : id(0), age(0) {}
};

int main() {
    // Do not embed credentials in the source code.
    const char* dsn = std::getenv("LITEORM_PG_DSN");
    if (!dsn || !*dsn) {
        std::cerr << "Set LITEORM_PG_DSN for a writable PostgreSQL test database.\n";
        return 2;
    }
    try {
        auto db = liteorm::Database::postgresql(dsn);
        db.model<User>("liteorm_demo_users")
            .primary_key("id", &User::id)
            .field("name", &User::name)
            .field("age", &User::age);
        liteorm::Repository<User> users = db.repository<User>();
        users.create_table();

        User alice;
        alice.name = "PostgreSQL Alice";
        alice.age = 25;
        users.create(alice);

        liteorm::Optional<User> found = users.find(alice.id);
        if (!found.has_value()) return 1;
        std::cout << "Found " << found.value().name << " (id=" << found.value().id << ")\n";

        std::vector<User> adults = users.where(liteorm::col(&User::age) >= 18)
            .order_by(&User::name).limit(10).all();
        std::cout << "Adult rows: " << adults.size() << "\n";

        alice.age = 26;
        if (users.update(alice) != 1) return 1;
        if (users.remove(alice.id) != 1) return 1;
        std::cout << "PostgreSQL CRUD completed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "PostgreSQL example failed: " << e.what() << "\n";
        return 1;
    }
}
