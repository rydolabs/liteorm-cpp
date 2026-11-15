# tinyorm-cpp11 v0.7-core — SQLite + PostgreSQL 경량 ORM

**C++11 Code-first ORM prototype** with a shared fluent API for SQLite3 and PostgreSQL (libpq). It provides type-checked column expressions, parameterized statements, CRUD, typed `INNER JOIN` / `LEFT JOIN` (including three or more tables), relation descriptors, batched one-to-many loading, explicit foreign key DDL, and RAII transactions.

**기본 기능 중심 개발 정책:** [BASIC_SCOPE.ko.md](BASIC_SCOPE.ko.md)를 참고하세요. v1.0의 목표는 CRUD, 타입 안전한 기본 조회, 트랜잭션, SQLite/PostgreSQL 호환성, 설치·테스트입니다. Connection Pool, Change Tracking, 자동 스키마 Diff, 고급 SQL 문법은 **필수 대상이 아닙니다**. 기존 다중 JOIN·관계·Migration 기능은 유지하지만 기능 확장은 보류합니다.

**Status:** educational/early MVP, not production-ready. Includes **explicit versioned SQL migrations**. Does not include automatic schema diffing, a migration CLI, connection pool, change tracking, self-join aliases, or automatic object navigation. Relations are explicitly defined and loaded on demand.

**시작하기:** [QUICKSTART.ko.md](QUICKSTART.ko.md)  ·  **검증 기록:** [VALIDATION.ko.md](VALIDATION.ko.md)

## Build requirements

- GCC or Clang with **C++11** support
- CMake >= 3.10
- SQLite3 headers/library (required)
- PostgreSQL **libpq** headers/library (`libpq-dev`, required by default; optionally disable)
- A running PostgreSQL server **only for the live integration test / PG demo**, not for compiling.

On Debian/Ubuntu: `sudo apt-get install cmake g++ libsqlite3-dev libpq-dev`

```bash
cmake -S . -B build \
  -DCMAKE_CXX_STANDARD=11 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCMAKE_CXX_EXTENSIONS=OFF -DTINYORM_WITH_POSTGRESQL=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/tinyorm_migrations_demo
./build/tinyorm_demo
./build/tinyorm_relations_demo
```

To **compile with SQLite only** (no libpq dependency):

```bash
cmake -S . -B build-sqlite -DTINYORM_WITH_POSTGRESQL=OFF -DCMAKE_CXX_STANDARD=11
cmake --build build-sqlite -j
ctest --test-dir build-sqlite --output-on-failure
```

## 기본 조회 헬퍼와 설치 패키지 (v0.5부터)

```cpp
// users is a registered Table<User> (see complete examples below).
std::int64_t matching = db.select(users)
    .where(users.age >= 18).count();
bool exists = db.select(users)
    .where(users.name == std::string("Alice")).exists();
```

`count()` / `exists()`는 WHERE만 적용합니다. `order_by()`와 `limit()`이 있어도
두 헬퍼에서는 무시됩니다. 기본 키가 자동 증가이거나 수동으로 지정한 **정수**이면
`db.insert()`가 그 키를 반환하고, 문자열 키/키 없음이면 `0`을 반환합니다.

CMake 패키지 설치 예시:

```bash
cmake -S . -B build -DTINYORM_WITH_POSTGRESQL=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/stage"
cmake -S tests/consumer -B consumer-build -DCMAKE_PREFIX_PATH="$PWD/stage"
cmake --build consumer-build
./consumer-build/tinyorm_installed_consumer
```

외부 CMake 프로젝트는 `find_package(tinyorm_cpp11 0.7 CONFIG REQUIRED)`와
`target_link_libraries(app PRIVATE tinyorm::tinyorm)`를 사용합니다.
`TINYORM_WITH_POSTGRESQL=OFF`는 libpq 없이 SQLite 전용으로 설치합니다.
PostgreSQL 실제 서버 테스트는 `TINYORM_TEST_PG_DSN` 환경변수가 필요합니다. [POSTGRESQL_TESTING.ko.md](POSTGRESQL_TESTING.ko.md)에 실행 방법과 실패/건너뜀 기준이 있습니다.

## v0.7-core: 기본 API 회귀 테스트·사용 가이드

- `tests/test_portable_core.cpp`: 같은 시나리오를 SQLite 및 PostgreSQL에 실행할 수 있는 공통 회귀 테스트를 추가했습니다. SQL 인젝션 형태의 문자열, 빈 문자열, UTF-8/한국어, `Optional<T>` NULL, 연속 WHERE, IN, 잘못된 SQL Dialect, Transaction move/자동 롤백, UNIQUE 위반 후 연결 재사용을 검사합니다.
- `insert_plan()`은 **등록된 컬럼이 없는 테이블**에 대해 명확한 `std::logic_error`를 발생시킵니다. 기존 공개 CRUD API는 그대로 유지했습니다.
- 텍스트의 NUL 바이트 처리 차이를 명시적으로 테스트합니다. **SQLite는 TEXT 안의 NUL을 보존**하지만 **PostgreSQL TEXT는 이를 거부**합니다. 과거 SQLite 동작을 변경하지 않았습니다.
- [QUICKSTART.ko.md](QUICKSTART.ko.md): 최소 예제, 두 DB 연결 방법, CMake 설치 및 오류 처리 안내.
- PostgreSQL Docker 테스트 가이드의 누락된 임시 DB 비밀번호 설정을 수정했습니다.
- **실제 PostgreSQL 서버 테스트는 현재 실행 환경에서 수행하지 못했습니다.** CI용 PostgreSQL 테스트는 제공되나 실행 결과를 확인하기 전에는 검증 완료로 표기하지 않습니다.

## v0.6-core: 기본 타입 및 오류 처리 안정화

- PostgreSQL `uint16_t` 필드는 `INTEGER`, `uint32_t`는 `BIGINT`로 생성해 부호 없는 값 전체 범위를 수용합니다. `uint64_t`는 SQL의 signed `BIGINT` 한계를 넘어가면 예외가 발생합니다.
- `ValueCodec<float>::decode`는 범위를 벗어난 DB 실수(예: `1e100`)를 무한대로 묵인하지 않고 `std::out_of_range`를 던집니다.
- SQLite `Database::execute()`는 읽기 전용 SQL 계획을 거부합니다. 0행을 반환한 SELECT가 이전 UPDATE 건수를 반환하던 오류를 방지합니다.
- 제약 조건 위반 → RAII 롤백 → 연결 재사용, 숫자 경계값, SQL 인젝션 유사 문자열에 대한 회귀 테스트를 추가했습니다.
- PostgreSQL CI 테스트에는 `TINYORM_REQUIRE_PG=1`을 설정해 서버 테스트가 우연히 건너뛰어진 채 성공하는 것을 방지합니다. 로컬에서 DSN이 없으면 기존처럼 건너뛸 수 있습니다.
- `ConnectionPool`, `Session`, 자동 Migration Diff, 고급 SQL DSL은 구현 범위에 추가하지 않았습니다.

## Versioned migrations (v0.4)

`MigrationRunner` supplies an explicit, source-controlled, **transactional** migration
registry for SQLite and PostgreSQL. Migration SQL is *trusted application source*, not
user-supplied text. Values in the internal history table are parameterized.

```cpp
#include "tinyorm/tinyorm.hpp"
#include <vector>

// db is an existing tinyorm::Database connected to SQLite or PostgreSQL.
void migrate(tinyorm::Database& db) {
    std::vector<tinyorm::Migration> changes;
    tinyorm::Migration users("20261115_0001_users");
    users.sqlite_up("CREATE TABLE app_users (id INTEGER PRIMARY KEY, email TEXT)")
         .postgresql_up("CREATE TABLE app_users (id BIGINT PRIMARY KEY, email TEXT)")
         .down("DROP TABLE app_users");
    changes.push_back(users);

    tinyorm::Migration index("20261115_0002_email_index");
    index.up("CREATE INDEX app_users_email_idx ON app_users(email)")
         .down("DROP INDEX app_users_email_idx");
    changes.push_back(index);

    tinyorm::MigrationRunner runner(db);
    const std::vector<std::string> waiting = runner.pending(changes);
    const tinyorm::MigrationReport report = runner.apply_all(changes);
    const std::vector<tinyorm::MigrationRecord> history = runner.applied();
    // Explicit destructive operation: caller chooses when to invoke.
    // runner.rollback_last(changes);
}
```

Use `create_table_sql(your_table, SqlDialect::SQLite/PostgreSQL)` to produce explicit
`CREATE TABLE` statements from C++11 metadata, as shown in
`examples/migrations_main.cpp`. This does **not** compare a database with a model or
produce ALTER statements automatically.

**Semantics and safeguards:**

- Migration IDs are ASCII identifiers, **strictly increasing lexicographically**; use
  fixed-width versions such as `0001`, `0002`, `0010`. Applied rows must exactly match a
  prefix of the registered versions. Deleting, inserting a historical version, or
  modifying applied SQL causes an error; no silent repair.
- The `_tinyorm_migrations` table holds version, checksum and timestamp. `pending()` and
  `applied()` create this table if absent, so even inspection performs minimal DDL.
- `.up()` and `.down()` contain portable/common SQL. `.sqlite_up()` / `.postgresql_up()`
  **replace the entire common up list** for the chosen backend if populated; similarly
  for the down list. Down steps execute **in declaration order**.
- `apply_all()` holds one transaction for the **entire batch** of pending migrations.
  Either every pending migration and its history record commits, or none do. SQLite
  uses `BEGIN IMMEDIATE`; PostgreSQL uses a transaction-scoped advisory lock before
  accessing/creating history. Do not nest it inside `db.transaction()`.
- `rollback_last()` only undoes the last applied migration and requires explicit down
  SQL. Rollback runs inside its own transaction. It does **not** automatically
  discover a safe inverse or restore deleted application data.
- Checksums are stable **FNV-1a 64-bit fingerprints**, meant to detect accidental
  source edits, **not cryptographic tamper protection**. They include both databases'
  SQL and both up/down lists, so a change to either backend's scripts invalidates
  the previously recorded checksum.
- SQL steps are *trusted raw SQL*: never interpolate untrusted input. Scripts must
  **not** contain transaction control (`BEGIN`, `COMMIT`, `ROLLBACK`), PostgreSQL
  non-transactional DDL (`CREATE INDEX CONCURRENTLY`), or other operations that cannot
  safely run inside the migrator's transaction. Review destructive scripts and take
  backups before applying in real systems.
- PostgreSQL advisory lock serializes cooperating tinyorm migration runners in the
  **same database**. SQLite writer locking serializes concurrent file-backed runners;
  separate `:memory:` connections are independent databases. No external migrator
  interoperability or cross-database coordination is provided.

For running locally, start with `./build/tinyorm_migrations_demo`. Live PostgreSQL
migration tests run only if `TINYORM_TEST_PG_DSN` points to a writable test database.

## One ORM API, two database backends

```cpp
#include "tinyorm/tinyorm.hpp"
#include <cstdint>
#include <string>

struct User {
    std::int64_t id;
    std::string name;
    int age;
    tinyorm::Optional<std::string> nickname;
    User() : id(0), age(0) {}
};

struct Users : tinyorm::Table<User> {
    tinyorm::Column<User, std::int64_t> id;
    tinyorm::Column<User, std::string> name;
    tinyorm::Column<User, int> age;
    tinyorm::Column<User, tinyorm::Optional<std::string> > nickname;

    Users() : Table("users"),
        id(add("id", &User::id, tinyorm::ColumnOptions::identity())),
        name(add("name", &User::name, tinyorm::ColumnOptions::unique_key())),
        age(add("age", &User::age)),
        nickname(add("nickname", &User::nickname)) {}
};

void example(tinyorm::Database& db) {
    Users users;
    db.create_table(users);

    User alice;
    alice.name = "Alice"; alice.age = 30;
    const std::int64_t id = db.insert(users, alice);

    const std::vector<User> adults = db.select(users)
        .where(users.age >= 18 && users.name != std::string("admin"))
        .order_by(users.name.asc()).limit(10).all();

    const std::unique_ptr<User> found = db.find(users, users.id, id);
    db.update(users).set(users.age, 31).where(users.id == id).execute();

    { // Rolls back unless tx.commit() is called.
        tinyorm::Transaction tx = db.transaction();
        db.remove(users).where(users.id == id).execute();
    }
}

int main() {
    tinyorm::Database sqlite(":memory:");
    example(sqlite);

    // Use a writable test DB; load credentials from environment in real applications.
    // tinyorm::Database postgres("host=localhost dbname=test ...",
    //     tinyorm::BackendKind::PostgreSQL);
    // example(postgres);
}
```

The same `Table`, `Column`, `select`, `insert`, `find`, `update`, `remove`, and `transaction` methods work with both backends. Prefer a PostgreSQL DSN from an environment variable rather than putting passwords into code.

### INNER JOIN and foreign keys

```cpp
struct Post {
    std::int64_t id, user_id;
    std::string title;
    Post() : id(0), user_id(0) {}
};
struct Posts : tinyorm::Table<Post> {
    tinyorm::Column<Post, std::int64_t> id, user_id;
    tinyorm::Column<Post, std::string> title;
    Posts() : Table("posts"),
        id(add("id", &Post::id, tinyorm::ColumnOptions::identity())),
        user_id(add("user_id", &Post::user_id,
            tinyorm::ColumnOptions::foreign_key("users", "id"))),
        title(add("title", &Post::title)) {}
};

// Posts posts; Users users;
// db.create_table(users); db.create_table(posts);
// std::vector<std::pair<User, Post> > rows = db.join(users, posts)
//     .on(users.id == posts.user_id)
//     .where(users.age >= 18).order_by(posts.title.asc()).all();
```

`JOIN` supports distinct table names, `ON`, `WHERE`, `ORDER BY`, `LIMIT`, `all()` and `first()`.
Two-table `INNER JOIN` returns `std::pair<Left,Right>`, preserving the v0.2 row shape. Two-table
`LEFT JOIN` returns `std::pair<Left,Optional<Right> >`; right rows are absent when the right
**primary key is NULL**, so a registered non-null primary key is required for a LEFT-joined
model. Three or more joined tables return `std::tuple<First,Second,...>`, with `Optional<Model>`
for each LEFT-joined model. Self-joins/duplicate table names require aliases and are not supported.

#### LEFT JOIN and multi-table JOIN

```cpp
// Users users; Posts posts; Tables created and populated above.
std::vector<std::pair<User, tinyorm::Optional<Post> > > rows =
    db.left_join(users, posts)
      .on(users.id == posts.user_id)
      .order_by(users.id.asc())
      .all();

// With a mapped Comments table/model:
// auto triples = db.join(users, posts)
//     .on(users.id == posts.user_id)
//     .left_join(comments)
//     .on(posts.id == comments.post_id)
//     .all();
// Each row is tuple<User, Post, Optional<Comment> >.
```

`ON` clauses can refer to the new table and any previously joined table. Each JOIN must
have an `on(...)` clause before another JOIN is appended; `where` and `order_by` only
accept registered columns from participating tables. Query values are bound as parameters.

#### Typed has-many and belongs-to relations

```cpp
const tinyorm::HasMany<User, Post, std::int64_t> user_posts =
    tinyorm::has_many(users, users.id, posts, posts.user_id);
std::vector<User> all_users = db.select(users).all();
std::vector<std::vector<Post> > posts_per_user = db.load_many(user_posts, all_users);
// posts_per_user[i] corresponds to all_users[i].

const tinyorm::BelongsTo<Post, User, std::int64_t> post_author =
    tinyorm::belongs_to(posts, posts.user_id, users, users.id);
// std::unique_ptr<User> author = db.load(post_author, a_post);
```

`load_many` deduplicates parent keys and queries children using bound `IN` lists,
with **500 keys per batch** to avoid old SQLite variable limits: 1 query for up to
500 distinct parents, more queries for larger inputs. This avoids one query per parent,
but is not a single-query guarantee at arbitrary scale. Duplicate parent keys are
supported. Neither parent/child results nor collections are automatically ordered
without explicit ordering; do not rely on incidental row order. Relations do **not**
add C++ member pointers to nested objects. A relation descriptor references the
original `Table` instances, which must outlive it.

**Parameterized IN** is also directly available: `db.select(users).where(users.id.in(keys)).all()`.
An empty list compiles to `(1 = 0)`; NULL in an `IN` list is rejected (use `is_null()`).

### SQL dialect example

For the same `users.age >= 18` predicate:

- SQLite: `WHERE "users"."age" >= ?`
- PostgreSQL: `WHERE "users"."age" >= $1`

Both use **parameterized APIs**, never string interpolation for values. SQLite uses `sqlite3_prepare_v2`/`sqlite3_bind_*`; PostgreSQL uses `PQexecParams` and `INSERT ... RETURNING "id"` for its generated primary keys.

### Live PostgreSQL integration test

The project includes `tests/test_postgresql.cpp` with schema creation, foreign keys, INSERT/RETURNING, SELECT, INNER/LEFT/multi JOIN, relation loaders, UPDATE, DELETE, NULL, SQL injection-shaped text, constraints, and transaction rollback/commit. It needs a **writable test database** and creates/drops test tables.

```bash
export TINYORM_TEST_PG_DSN='host=127.0.0.1 port=5432 dbname=tinyorm_test user=tinyorm password=your_test_password connect_timeout=5'
ctest --test-dir build --output-on-failure
```

Without `TINYORM_TEST_PG_DSN`, CTest explicitly marks both PostgreSQL integration tests **Skipped**, not Passed. The build and SQL-dialect unit tests still run. The included GitHub Actions workflow starts an ephemeral PostgreSQL 17 service and runs both database integrations on push/PR (when used in a repository).

For a simple Postgres CLI demo:

```bash
export TINYORM_PG_DSN='host=127.0.0.1 dbname=tinyorm_test user=tinyorm ...'
./build/tinyorm_pg_demo
```

## Type mapping

| C++ type | SQLite | PostgreSQL |
|---|---|---|
| integer up to 16 bits | `INTEGER` | `SMALLINT` |
| 32-bit integer | `INTEGER` | `INTEGER` |
| 64-bit integer | `INTEGER` | `BIGINT` |
| `bool` | `INTEGER` (0 or 1) | `BOOLEAN` |
| `float` | `REAL` | `REAL` |
| `double` | `REAL` | `DOUBLE PRECISION` |
| `std::string` | `TEXT` | `TEXT` |
| `Optional<T>` | nullable `T` | nullable `T` |
| generated `int64_t` primary key | `INTEGER PRIMARY KEY AUTOINCREMENT` | `BIGSERIAL PRIMARY KEY` |

Notes: PostgreSQL text cannot contain NUL bytes; the driver **rejects** such values rather than truncating. SQLite supports embedded NUL in TEXT and is tested for this. Unsigned 64-bit values above `INT64_MAX`, non-finite floats, unsupported PG OIDs, binary types, datetime, and arbitrary SQL conversions are not supported. Supported model fields must be default-constructible/assignable.

## Compatibility, safety, and limitations

1. Code uses only **C++11** (`-std=c++11`, no `std::optional`, `std::variant`, reflection, or coroutines); v0.1 SQLite users can still write `Database(":memory:")` or include `tinyorm/sqlite.hpp`.
2. SQL values are always parameterized, identifier names from mapping are double-quote escaped. `execute_sql()` is trusted raw SQL intended for DDL/settings; never concatenate user input into it.
3. SQL-plan dialect mismatch is rejected. Bound query `compile()` methods choose the database dialect automatically.
4. `Database`, tables, live result statements, and transactions are **not safe to share concurrently** or destroy out of order. Active queries and transactions need the database to outlive them. No connection pool.
5. Model DDL is deliberately minimal. `create_table()` executes create-only DDL; `create_table_sql()` generates SQL for explicitly registered migrations. There is no automatic schema diff, composite primary keys, or custom SQL types. PostgreSQL generated IDs use `SERIAL/BIGSERIAL` (sequence-based); this is not SQL-standard `GENERATED AS IDENTITY`.
6. `insert()` returns an integer key for PostgreSQL registered primary-key models and SQLite last-insert rowid. PostgreSQL models **without a registered primary key** return 0. A PostgreSQL non-integer registered key is not supported by this `insert()` return type.
7. Multi-row bulk updates and deletes require `.where(...)` or explicit `.all_rows()`. `find()` requires the mapped primary key.
8. Single-table query `where()` rejects references to other tables; JOIN predicates/conditions are scoped to their participating tables. Self-joins require future aliases. Relations support explicit batch loading, not implicit JOINs, auto-hydrated navigation properties, or an identity map.
9. For PostgreSQL, `PostgresError::sqlstate()` exposes SQLSTATE for query errors; SQLite errors expose numeric SQLite error codes. Server-connection errors may have empty SQLSTATE.
10. C++11 source compatibility is **not** binary ABI compatibility across compiler/standard library combinations. Real production use requires more fuzzing, PostgreSQL concurrency/load tests, connection retry strategy, secure credential storage, backups, and performance benchmarking.

## Project layout

```text
include/tinyorm/core.hpp          C++11 metadata, codecs, conditions, dialect plans, joins
include/tinyorm/database.hpp      portable Database/Statement/Transaction, model DDL and bound queries
include/tinyorm/migration.hpp     explicit Migration/MigrationRunner public API
include/tinyorm/sqlite.hpp        backward-compatible header
include/tinyorm/postgresql.hpp    PostgreSQL convenience header
src/backend.hpp                  private backend/cursor interfaces
src/database.cpp                 shared facade + RAII transaction implementation
src/migration.cpp                versioned SQL migration executor
src/sqlite.cpp                   SQLite backend
src/postgresql.cpp               PostgreSQL libpq backend
src/postgresql_disabled.cpp      optional disabled-backend stub
examples/main.cpp               SQLite example
examples/postgresql_main.cpp    PostgreSQL example
examples/relations_main.cpp     LEFT JOIN and batched relation loading demo
examples/migrations_main.cpp    SQLite migration demonstration
 tests/test_sqlite.cpp           SQLite CRUD and edge cases
 tests/test_join.cpp             SQLite FK and INNER JOIN
 tests/test_dialects.cpp         dialect placeholder/parameter assertions
 tests/test_postgresql.cpp       PostgreSQL live integration (requires DSN)
 tests/test_relations.cpp        SQLite LEFT/MULTI JOIN, relationships, IN and PG SQL compilation
 tests/test_migrations.cpp       SQLite version, checksum, rollback and failure tests
 tests/test_migration_concurrency.cpp   concurrent SQLite migration serialization
 tests/test_postgresql_migrations.cpp  live PostgreSQL migration tests (requires DSN)
.github/workflows/ci.yml         PostgreSQL 17 service integration CI
```

## Next milestones

- Explicit table aliases for self-joins; per-table projections, named relations, and `include()` API.
- Schema diff/snapshot generation, migration CLI, forward-only production policies, and repair tooling.
- Connection pooling, timeouts, cancellation, monitoring, and extensive PostgreSQL live stress tests.
- Optional unit-of-work/change tracking and identity map after the query layer stabilizes.
