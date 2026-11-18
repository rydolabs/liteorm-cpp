# LiteORM-cpp

Lightweight, C++11-compatible **code-first ORM** for **SQLite** and **PostgreSQL**.

> Status: **0.8.1 object-first API stability preview**. C++11; not yet production-ready. PostgreSQL 17 live integration is tested via GitHub Actions.

## Object-first API (v0.8.1 preview)

```cpp
auto db = liteorm::Database::sqlite("app.db");
db.model<User>("users").primary_key("id", &User::id)
    .field("name", &User::name).field("age", &User::age);
auto users = db.repository<User>();
users.create_table();
User alice; alice.name = "Alice"; alice.age = 27;
users.create(alice);             // void; populates generated alice.id
auto found = users.find(alice.id); // Optional<User>
auto adults = users.where(liteorm::col(&User::age) >= 18)
    .order_by(&User::name).limit(10).all();
users.update(alice);
users.remove(alice.id);
```

Define User as a regular struct; no custom Table subclass or hand-written SQL is required.
For larger applications, register models once in a helper:

```cpp
void register_models(liteorm::Database& db) {
    db.model<User>("users")
      .primary_key("id", &User::id)
      .field("name", &User::name)
      .field("age", &User::age);
}
```

After connecting, call `register_models(db)` and then `db.repository<User>()`.
Database moves now throw `std::logic_error` while Repository/RepositoryQuery, Statement or Transaction handles are alive. If a Database is destroyed, subsequent Repository operations throw instead of dereferencing a dangling pointer. Treat handles as Database-scoped; this protection is specific to the Object-first API and does not make concurrent access safe. See [v0.8.1 stability notes](STABILITY_V081.ko.md).
**`Repository<T>::create(T&)` returns `void` (finalized in v0.8.1).**
Successful inserts fill generated integer keys on the input object; failed inserts throw.
Migration from the earlier preview: replace `auto id = users.create(user)` with
`users.create(user); auto id = user.id;`.
The legacy `Database::insert(table, object)` return type is unchanged.
See [create() API contract (Korean)](CREATE_API_CONTRACT.ko.md),
[Object API guide](OBJECT_API.ko.md) and [Repository demo](examples/repository_main.cpp).

## Database connections and runnable examples

Only Database construction differs between the two backends; the Repository API is identical.

**SQLite:**

```cpp
auto db = liteorm::Database::sqlite("app.db");     // Persistent database file
// auto db = liteorm::Database::sqlite(":memory:"); // In-memory database
```

**PostgreSQL:**

```cpp
const char* dsn = std::getenv("LITEORM_PG_DSN");
if (!dsn || !*dsn) throw std::runtime_error("LITEORM_PG_DSN not set");
auto db = liteorm::Database::postgresql(dsn);
```

Runnable C++11 examples: [SQLite](examples/sqlite_object_main.cpp) and
[PostgreSQL](examples/postgresql_object_main.cpp).

Build and run them with the instructions in
[the SQLite/PostgreSQL guide (Korean)](DATABASE_CONNECTIONS.ko.md).

## Features

- C++11 API using typed `Table<T>` / `Column<T, Field>` model metadata.
- Parameterized CRUD (`insert`, `find`, `select`, `update`, `remove`), filters, ordering, limit, `count` and `exists`.
- INNER/LEFT JOIN, relation descriptors and optional batched one-to-many loading.
- SQLite and PostgreSQL (`libpq`) using the same public query API.
- RAII transactions; explicit SQL migrations.
- CMake install and `find_package(liteorm_cpp CONFIG REQUIRED)`.

**Out of scope for the core:** connection pool, change tracking, automatic schema diff, async, and advanced query DSL.

## Build

Dependencies: CMake >=3.10, C++11 compiler, SQLite3 dev files, optional PostgreSQL libpq dev files.

```bash
cmake -S . -B build -DLITEORM_WITH_POSTGRESQL=ON
cmake --build build -j 2
ctest --test-dir build --output-on-failure
```

SQLite only:

```bash
cmake -S . -B build-sqlite -DLITEORM_WITH_POSTGRESQL=OFF
cmake --build build-sqlite -j 2
ctest --test-dir build-sqlite --output-on-failure
```

## Legacy Table/SQL API

The original Table/SQL-style API is supported for compatibility. New projects
should use the Object-first Repository API above.


```cpp
#include <liteorm/liteorm.hpp>
#include <cstdint>
#include <string>

struct User {
    std::int64_t id;
    std::string name;
    int age;
    User() : id(0), age(0) {}
};

struct Users : liteorm::Table<User> {
    liteorm::Column<User, std::int64_t> id;
    liteorm::Column<User, std::string> name;
    liteorm::Column<User, int> age;
    Users() : Table("users"),
        id(add("id", &User::id, liteorm::ColumnOptions::identity())),
        name(add("name", &User::name)),
        age(add("age", &User::age)) {}
};

int main() {
    liteorm::Database db(":memory:");
    Users users;
    db.create_table(users);
    User alice; alice.name = "Alice"; alice.age = 28;
    db.insert(users, alice);
    std::vector<User> adults = db.select(users).where(users.age >= 18).all();
    return adults.size() == 1 ? 0 : 1;
}
```

Prefer `liteorm::Database::postgresql(conninfo)` for new code; the legacy constructor stays available.

## Install / consume

```bash
cmake --install build --prefix "$PWD/stage"
cmake -S tests/consumer -B consumer-build -DCMAKE_PREFIX_PATH="$PWD/stage"
cmake --build consumer-build
./consumer-build/liteorm_installed_consumer
```

In your CMake project:

```cmake
find_package(liteorm_cpp 0.8 CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE liteorm::liteorm)
```

## Backward compatibility

The old headers `<tinyorm/...>`, namespace `tinyorm::`, CMake package `tinyorm_cpp11` and target `tinyorm::tinyorm` remain available as compatibility shims. New code should use **`liteorm`**. See [MIGRATION.md](MIGRATION.md).

## Testing PostgreSQL

Set `LITEORM_TEST_PG_DSN` to a **disposable writable test database** before running CTest. The CI workflow runs a PostgreSQL service and sets `LITEORM_REQUIRE_PG=1` to avoid silently skipping backend tests. No live PostgreSQL result is claimed for this local migration.

See [QUICKSTART.ko.md](QUICKSTART.ko.md) and [BASIC_SCOPE.ko.md](BASIC_SCOPE.ko.md). Earlier project documentation is archived at [docs/history](docs/history/).
