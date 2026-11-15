#include "liteorm/liteorm.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

struct User {
    std::int64_t id;
    std::string name;
    int age;
    liteorm::Optional<std::string> nickname;
    bool active;
    double score;
    User() : id(0), age(0), active(false), score(0) {}
};

struct UserTable : liteorm::Table<User> {
    liteorm::Column<User, std::int64_t> id;
    liteorm::Column<User, std::string> name;
    liteorm::Column<User, int> age;
    liteorm::Column<User, liteorm::Optional<std::string> > nickname;
    liteorm::Column<User, bool> active;
    liteorm::Column<User, double> score;
    UserTable() : Table("users"),
        id(add("id", &User::id, liteorm::ColumnOptions::identity())),
        name(add("name", &User::name, liteorm::ColumnOptions::unique_key())),
        age(add("age", &User::age)),
        nickname(add("nickname", &User::nickname)),
        active(add("active", &User::active)),
        score(add("score", &User::score)) {}
};

template <typename E, typename F>
void must_throw(F f) {
    bool caught = false;
    try { f(); } catch (const E&) { caught = true; }
    if (!caught) throw std::runtime_error("Expected exception not thrown");
}

int main() {
    UserTable users;
    liteorm::Database db(":memory:");
    db.create_table(users);
    db.create_table(users); // idempotent

    liteorm::SqlPlan plan = db.select(users)
        .where((users.age >= 18 && users.name != std::string("admin")) ||
               !users.nickname.is_null())
        .order_by(users.name.desc())
        .limit(4).compile();
    assert(plan.sql ==
           "SELECT \"users\".\"id\", \"users\".\"name\", \"users\".\"age\", "
           "\"users\".\"nickname\", \"users\".\"active\", \"users\".\"score\" "
           "FROM \"users\" WHERE ((\"users\".\"age\" >= ? AND \"users\".\"name\" <> ?) "
           "OR (NOT (\"users\".\"nickname\" IS NULL))) ORDER BY \"users\".\"name\" DESC LIMIT ?");
    assert(plan.params.size() == 3);
    assert(plan.params[0].integer == 18);
    assert(plan.params[1].text == "admin");
    assert(plan.params[2].integer == 4);

    User alice;
    alice.name = "Alice";
    alice.age = 25;
    alice.nickname = std::string("Al");
    alice.active = true;
    alice.score = 99.5;
    const std::int64_t alice_id = db.insert(users, alice);
    assert(alice_id == 1);

    User bob;
    bob.name = "Bob";
    bob.age = 17;
    bob.score = 80.25;
    const std::int64_t bob_id = db.insert(users, bob);

    const std::string dangerous = "Robert'); DROP TABLE users;--";
    User malicious;
    malicious.name = dangerous;
    malicious.age = 52;
    malicious.active = true;
    malicious.nickname = std::string("nul\0byte", 8);
    const std::int64_t dangerous_id = db.insert(users, malicious);

    std::vector<User> adults = db.select(users).where(users.age >= 18)
        .order_by(users.name.asc()).all();
    assert(adults.size() == 2);
    assert(adults[0].name == "Alice");
    assert(adults[1].name == dangerous);
    assert(adults[1].nickname.has_value());
    assert(adults[1].nickname.value().size() == 8);

    assert(db.select(users).where(users.nickname.is_null()).all().size() == 1);
    assert(db.select(users).where(users.nickname.is_not_null()).all().size() == 2);
    assert(db.select(users).where(!users.nickname.is_null()).all().size() == 2);
    must_throw<std::invalid_argument>([&]() {
        (void)(users.nickname == liteorm::Optional<std::string>());
    });
    assert(db.select(users).where(users.age > 0).limit(0).all().empty());
    assert(db.select(users).where(users.age > 100).first().get() == NULL);

    std::unique_ptr<User> a = db.find(users, users.id, alice_id);
    assert(a.get() && a->id == alice_id && a->nickname.value() == "Al");
    assert(a->active && a->score == 99.5);
    std::unique_ptr<User> b = db.find(users, users.id, bob_id);
    assert(b.get() && !b->nickname.has_value());
    assert(!b->active);

    assert(db.update(users).set(users.age, 26).set(users.active, false)
               .where(users.id == alice_id).execute() == 1);
    assert(db.find(users, users.id, alice_id)->age == 26);
    assert(!db.find(users, users.id, alice_id)->active);
    assert(db.update(users).set(users.age, 100).where(users.id == 999).execute() == 0);

    must_throw<std::logic_error>([&]() { db.remove(users).execute(); });
    must_throw<std::logic_error>([&]() { db.update(users).set(users.age, 0).execute(); });
    must_throw<std::logic_error>([&]() { db.update(users).where(users.age == 1).execute(); });
    must_throw<std::invalid_argument>([&]() {
        db.update(users).set(users.id, static_cast<std::int64_t>(3));
    });
    must_throw<std::invalid_argument>([&]() {
        db.update(users).set(users.age, 2).set(users.age, 3);
    });
    must_throw<std::invalid_argument>([&]() { db.select(users).limit(-1); });
    must_throw<std::invalid_argument>([&]() { db.find(users, users.age, 1); });

    UserTable other_instance;
    must_throw<std::invalid_argument>([&]() {
        db.select(users).where(other_instance.age >= 18);
    });
    must_throw<std::invalid_argument>([&]() {
        db.select(users).order_by(other_instance.age.asc());
    });
    must_throw<std::invalid_argument>([&]() {
        db.update(users).set(other_instance.age, 12);
    });

    // A failed unique constraint must never execute a second statement.
    must_throw<liteorm::SqliteError>([&]() { db.insert(users, alice); });
    assert(db.select(users).all().size() == 3);

    { // Implicit rollback from RAII destructor.
        liteorm::Transaction transaction = db.transaction();
        assert(db.remove(users).where(users.id == alice_id).execute() == 1);
        assert(!db.find(users, users.id, alice_id));
        must_throw<std::logic_error>([&]() { db.transaction(); });
    }
    assert(db.find(users, users.id, alice_id).get() != NULL);

    { // Explicit rollback.
        liteorm::Transaction transaction = db.transaction();
        db.remove(users).where(users.id == bob_id).execute();
        transaction.rollback();
    }
    assert(db.find(users, users.id, bob_id).get() != NULL);

    { // Explicit commit.
        liteorm::Transaction transaction = db.transaction();
        assert(db.remove(users).where(users.id == bob_id).execute() == 1);
        transaction.commit();
    }
    assert(!db.find(users, users.id, bob_id));

    assert(db.remove(users).where(users.id == dangerous_id).execute() == 1);
    assert(db.select(users).all().size() == 1);
    assert(db.remove(users).all_rows().execute() == 1);
    assert(db.select(users).all().empty());

    // Sqlite integer mapping checks, null handling and identifier quoting.
    must_throw<std::out_of_range>([]() {
        liteorm::ValueCodec<unsigned long long>::encode(
            std::numeric_limits<unsigned long long>::max());
    });
    must_throw<std::out_of_range>([]() {
        liteorm::ValueCodec<unsigned int>::decode(liteorm::Value(static_cast<std::int64_t>(-1)));
    });
    must_throw<std::runtime_error>([]() {
        liteorm::ValueCodec<int>::decode(liteorm::Value::null());
    });
    must_throw<std::invalid_argument>([]() { liteorm::quote_ident(""); });
    assert(liteorm::quote_ident("a\"b") == "\"a\"\"b\"");
    assert(liteorm::ValueCodec<liteorm::Optional<int> >::decode(liteorm::Value::null())
               .has_value() == false);

    std::cout << "All C++11 SQLite ORM integration tests passed.\n";
    return 0;
}
