#include "liteorm/liteorm.hpp"
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

struct User {
    std::int64_t id;
    std::string name;
    int age;
    liteorm::Optional<std::string> nickname;
    User() : id(0), age(0) {}
};

struct UserTable : liteorm::Table<User> {
    liteorm::Column<User, std::int64_t> id;
    liteorm::Column<User, std::string> name;
    liteorm::Column<User, int> age;
    liteorm::Column<User, liteorm::Optional<std::string> > nickname;

    UserTable() : Table("users"),
        id(add("id", &User::id, liteorm::ColumnOptions::identity())),
        name(add("name", &User::name, liteorm::ColumnOptions::unique_key())),
        age(add("age", &User::age)),
        nickname(add("nickname", &User::nickname)) {}
};

int main() {
    liteorm::Database db(":memory:");
    UserTable users;
    db.create_table(users);

    User alice;
    alice.name = "Alice";
    alice.age = 25;
    alice.nickname = std::string("Al");
    const std::int64_t alice_id = db.insert(users, alice);

    User bob;
    bob.name = "Bob";
    bob.age = 17;
    db.insert(users, bob);

    const std::vector<User> adults = db.select(users)
        .where(users.age >= 18 && users.name != std::string("admin"))
        .order_by(users.name.asc())
        .limit(10).all();

    for (std::size_t i = 0; i < adults.size(); ++i)
        std::cout << adults[i].id << " " << adults[i].name << " " << adults[i].age << "\n";

    db.update(users).set(users.age, 26).where(users.id == alice_id).execute();
    std::unique_ptr<User> found = db.find(users, users.id, alice_id);
    std::cout << "Alice's updated age: " << found->age << "\n";

    {
        liteorm::Transaction tx = db.transaction();
        db.remove(users).where(users.id == alice_id).execute();
        // No commit => automatic rollback.
    }
    std::cout << "After rollback Alice exists: "
              << (db.find(users, users.id, alice_id) ? "yes" : "no") << "\n";
}
