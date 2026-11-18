# LiteORM-cpp

[![C++11 SQLite + PostgreSQL CI](https://github.com/rydolabs/liteorm-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/rydolabs/liteorm-cpp/actions/workflows/ci.yml)

**A lightweight, object-first ORM for C++11**, supporting **SQLite** and **PostgreSQL**.

Map ordinary C++ structs once, then create, find, update, and remove objects through a small typed Repository API. **Basic CRUD does not require writing SQL.**

> **Status:** v0.8.1 preview, not yet production-ready. The object-first API is recommended for new projects; the legacy Table/SQL API remains supported. [한국어 빠른 시작](QUICKSTART.ko.md) · [DB 연결 가이드](DATABASE_CONNECTIONS.ko.md)

## Quick start: SQLite

### Build and run

Requirements: CMake 3.10+, a C++11 compiler and SQLite3 development headers/libraries. No PostgreSQL/libpq installation is needed for this build.

```bash
cmake -S . -B build-sqlite -DLITEORM_WITH_POSTGRESQL=OFF
cmake --build build-sqlite -j 2
ctest --test-dir build-sqlite --output-on-failure

# Complete object-first example; creates liteorm_demo.sqlite3
./build-sqlite/liteorm_sqlite_object_example
```

### A complete C++11 example

```cpp
#include <liteorm/liteorm.hpp>
#include <cstdint>
#include <string>
#include <vector>

struct User {
    std::int64_t id;
    std::string name;
    int age;

    User() : id(0), age(0) {}
};

int main() {
    // Persistent file. Use ":memory:" for an in-memory SQLite DB.
    auto db = liteorm::Database::sqlite("app.db");

    // Register model metadata once per Database instance.
    db.model<User>("users")
        .primary_key("id", &User::id)
        .field("name", &User::name)
        .field("age", &User::age);

    auto users = db.repository<User>();
    users.create_table(); // Example only; prefer migrations in production.

    User alice;
    alice.name = "Alice";
    alice.age = 25;
    users.create(alice); // Returns void; generated ID is in alice.id.

    auto found = users.find(alice.id); // liteorm::Optional<User>
    if (!found.has_value()) return 1;

    std::vector<User> adults = users
        .where(liteorm::col(&User::age) >= 18)
        .order_by(&User::name)
        .limit(10)
        .all();

    alice.age = 26;
    if (users.update(alice) != 1) return 1;
    if (users.remove(alice.id) != 1) return 1;

    return adults.size() == 1 ? 0 : 1;
}
```

Full source: [SQLite object-first example](examples/sqlite_object_main.cpp).

## PostgreSQL: same ORM, different connection

Build with PostgreSQL/libpq development headers installed:

```bash
cmake -S . -B build -DLITEORM_WITH_POSTGRESQL=ON
cmake --build build -j 2
```

Replace only the Database construction line in your application. Load the connection string from your environment rather than embedding credentials in source code:

```cpp
#include <cstdlib>
#include <stdexcept>

const char* dsn = std::getenv("LITEORM_PG_DSN");
if (!dsn || !*dsn) throw std::runtime_error("LITEORM_PG_DSN not set");
auto db = liteorm::Database::postgresql(dsn);
```

For the runnable example, connect to a **writable test database**:

```bash
export LITEORM_PG_DSN='host=127.0.0.1 port=5432 dbname=liteorm_demo user=myuser connect_timeout=5'
# Set authentication through libpq/.pgpass or appropriate environment variables.
./build/liteorm_postgresql_object_example
```

**Everything after creating `db` — model mapping, Repository, CRUD and predicates — is identical for SQLite and PostgreSQL.** The examples remove their inserted rows but leave their example tables behind.

Full source: [PostgreSQL object-first example](examples/postgresql_object_main.cpp) · [Connection guide (Korean)](DATABASE_CONNECTIONS.ko.md).

## Repository API

Register a C++ model using `db.model<T>("table").primary_key(...).field(...)`, then call `db.repository<T>()`.

| Operation | Example | Return / behavior |
| --- | --- | --- |
| Create | `users.create(user);` | `void`; fills generated integer ID on `user` |
| Find by primary key | `users.find(id)` | `liteorm::Optional<User>` |
| Find all | `users.find_all()` | `std::vector<User>` |
| Find by member | `users.find_by(&User::name, "Alice")` | `std::vector<User>` |
| Filter | `users.where(liteorm::col(&User::age) >= 18).all()` | `std::vector<User>` |
| Sort / limit | `users.order_by(&User::name).limit(10).all()` | `std::vector<User>` |
| Update | `users.update(user)` | Number of affected rows (`int`) |
| Delete | `users.remove(id)` | Number of affected rows (`int`) |
| Count / exists | `users.count()` / `users.exists()` | `std::int64_t` / `bool` |

Predicates use C++ member pointers instead of string field names. Supported expressions include comparisons, `&&` / `||` / `!`, `in()`, `is_null()` and `is_not_null()`. Queries also offer `order_by_desc()` and `first()`.

### Primary keys, missing records and errors

- **Generated integer key:** `.primary_key("id", &User::id)` is the default. On success, `create(user)` assigns `user.id`.
- **Manually assigned key:** for example `.primary_key("id", &Account::id, false)` allows string or integer keys set by the caller.
- **Finalized in v0.8.1:** `Repository<T>::create(T&)` returns **`void`** and throws on insert failure. Change old preview code `auto id = users.create(user);` to `users.create(user); auto id = user.id;` for generated integer keys.
- `find(id)` yields an empty Optional if no row matches. `find_by()` returns zero or more results.
- `update()` writes every mapped non-key field (there is no automatic change tracking). `update()` and `remove()` return `0` when no row matches.
- The legacy low-level `Database::insert(table, object)` retains its `std::int64_t` return type.

See [`create()` contract](CREATE_API_CONTRACT.ko.md) and [Object-first API guide](OBJECT_API.ko.md).

## Transactions and safety

Repository operations can be grouped using the existing RAII transaction API:

```cpp
{
    liteorm::Transaction tx = db.transaction();
    users.create(alice);
    // More Repository operations against the same Database...
    tx.commit();
} // Without commit(), an active transaction is rolled back on destruction.
```

Do not use a single Database concurrently from multiple threads. Keep Repository/query objects within the Database lifetime; moving a Database while dependent Repository, query, Statement or Transaction objects exist throws. A Repository used after its Database is destroyed throws instead of dereferencing a dangling Database pointer. Legacy Table/SQL handles do not necessarily have the same guarantees.

Use parameterized ORM operations for untrusted values. Low-level `execute_sql()` and migration SQL are intended only for **trusted SQL**. Production schema changes should use reviewed migrations rather than `create_table()`.

[v0.8.1 stability notes](STABILITY_V081.ko.md) · [Migration guide](MIGRATION.md).

## CMake install and external projects

The `LITEORM_WITH_POSTGRESQL` option defaults to **ON**; select **OFF** explicitly when you only need SQLite and want to avoid libpq. Both configurations produce the `liteorm::liteorm` CMake target.

```bash
# Install the SQLite-only build from the earlier step:
cmake --install build-sqlite --prefix "$PWD/stage"
```

In your own CMake project:

```cmake
cmake_minimum_required(VERSION 3.10)
project(my_app LANGUAGES CXX)

find_package(liteorm_cpp 0.8.1 CONFIG REQUIRED)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE liteorm::liteorm)
```

Configure your consumer project with `-DCMAKE_PREFIX_PATH=/path/to/stage`. See the [installed-package consumer example](tests/consumer/).

For real PostgreSQL integration tests, set `LITEORM_TEST_PG_DSN` to a **disposable writable test database** before running `ctest --test-dir build --output-on-failure`. Set `LITEORM_REQUIRE_PG=1` if missing test credentials should fail rather than skip tests. GitHub Actions covers GCC, Clang, SQLite-only builds and a live PostgreSQL 17 instance.

## Project scope and compatibility

**Included:** object-first CRUD, typed filters, ordering, count/exists, SQLite/PostgreSQL backends, RAII transactions, and explicit SQL migrations.

**Deliberately omitted:** change tracking, connection pools, automatic schema diff, asynchronous database operations and a large SQL-like query DSL.

For compatibility, the older `Table<T>` / `Column` APIs, joins, relation helpers and low-level SQL operations remain available. They are **not** the recommended starting point for new projects. Legacy `<tinyorm/...>` headers, the `tinyorm::` namespace alias, `tinyorm_cpp11` package and `tinyorm::tinyorm` target also remain available; binary/ABI compatibility is not guaranteed.

[Quick start (Korean)](QUICKSTART.ko.md) · [DB connection guide (Korean)](DATABASE_CONNECTIONS.ko.md) · [Changelog](CHANGELOG.md) · [Project scope](BASIC_SCOPE.ko.md)
