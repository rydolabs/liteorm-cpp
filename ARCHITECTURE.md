> 개발 방향: [BASIC_SCOPE.ko.md](BASIC_SCOPE.ko.md) 참조. v0.7-core는 기존 아키텍처를 유지하면서 기본 CRUD의 회귀 테스트와 사용 예제를 보강한다.

# Architecture — liteorm-cpp11 v0.4

## Public API and C++11 constraints

`Database(connection, BackendKind)` selects the backend. `Table<Model>` registers C++11
member pointers and column metadata without reflection or C++17 features.
`Column<Model,Field>` constructs a typed expression (`Expr`) rather than interpolating SQL.
`Expr` nodes include comparison, column comparison, AND/OR/NOT, IS NULL/NOT NULL, and
bound `IN(...)` lists. The public model and query API works with SQLite and PostgreSQL.

```text
Application models (C++11)
  |
  +-- Table / Column / HasMany / BelongsTo
  +-- Query / JoinChain<First, JoinedPart<T,bool>...> / UpdateQuery / DeleteQuery
  |        |
  |        +-- Expr AST and SqlPlan{SQL, params, dialect}
  |                      |  SQLite: ?    PostgreSQL: $1, $2 ...
  +-- Migration / MigrationRunner (version registry + transactional DDL)
  +-- Database / Statement / Transaction
            |
            +-- detail::Backend (virtual interface)
                  +-- SQLite  (prepare/bind/step, streaming cursor)
                  +-- libpq   (PQexecParams, buffered PGresult)
```

## Typed joins

- The old public two-table `JoinQuery<A,B>` is retained as a standalone SQL builder.
  `Database::join(A,B)` now uses the generalized `JoinChain` executor.
- `db.join(a,b).on(...).join(c).on(...)` is an INNER/INNER query.
  `db.left_join(a,b)` and `.left_join(c)` select outer joins.
- Each `JoinedPart<T,true>` has a row type of `Optional<T>`, detected by the joined
  table's non-null primary key. A left-joined table without a primary key is rejected.
- Two-table results are `std::pair<A,B>` or `std::pair<A,Optional<B>>`; 3+ tables
  return `std::tuple` in join order. `Statement` columns are decoded by offset,
  and the projected columns are explicit and correspond to the registered model fields.
- Owners are checked in `Expr::validate_scope`; a reference to a table not in the
  current JOIN is an error. All table names must be distinct; aliases are not available.
- SQL parameters are emitted in query order: ON clauses (left-to-right), WHERE,
  LIMIT. SQLite and PostgreSQL both bind values separately from SQL text.

## Relationship metadata and loading

- `has_many(parent, parent_pk, child, child_fk)` registers a typed relationship descriptor,
  not a C++ navigation property, and does not create FK DDL automatically.
- `belongs_to(child, fk, parent, pk)` is a typed single-parent loader; `pk` must
  be the actual registered primary key.
- `Database::load_many` maps distinct parent keys to original parent indexes, then
  queries matching child rows via batched `WHERE fk IN (?,...)` (500 distinct keys
  per query). The child lists are returned aligned with input parent positions.
- `load_many` is O((P+C) log P) per batch for mapping; ordering of child rows is not
  promised without a future relation-level ORDER BY option.
- Parent and child model types must have default constructors and assignable fields.
  Relation key types must support `std::map` ordering (e.g., integers or string).
- Caller must ensure the mapped `Table` objects, and the `Database`, outlive bound queries
  and relation descriptors. Model descriptors are not thread-safe for concurrent mutation.

## Database safety and limitations

- Values are passed out-of-band as prepared/bound SQL parameters. `execute_sql` is
  intentionally trusted raw SQL for DDL/settings only; never interpolate input.
- `Transaction` uses rollback-on-destruction (RAII); no nested transactions or pool.
- Postgres uses synchronous `PQexecParams`; no server-side streaming or query cancel.
- `create_table()` is create-only; `create_table_sql(table, dialect)` provides DDL for explicit versioned migrations. No automatic schema diff. PG generated keys use SERIAL/BIGSERIAL.
- No self-join aliases, projections, result identity map, cascade object persistence,
  `include()` graph hydration or composite primary keys. Schema evolution is through explicitly registered migration SQL.
- No compatibility guarantee for compiled ABI across different compilers/stdlibs.

## Verification

- GCC strict C++11 flags (`-Wall -Wextra -Werror -pedantic`) and CMake tests.
- SQLite live CRUD, typed INNER/LEFT/multi JOIN, FK, batched relations (1205 parent
  inputs), SQL IN, and transaction tests.
- PostgreSQL `libpq` compiles/links; PG dialect compilation verifies placeholder
  order, including JOIN + WHERE + LIMIT. PostgreSQL live tests are included and
  executed in GitHub Actions with a database service; local execution is **skipped**
  if `LITEORM_TEST_PG_DSN` is absent, not claimed as a passing live test.

## Migration execution (v0.4)

- Application supplies an ordered vector of `Migration` definitions. A migration has a
  validated version and portable up/down SQL lists, optionally overridden per dialect.
- `MigrationRunner::apply_all()` first validates registry order, opens one database
  transaction, obtains a backend-specific migration lock, creates/reads the history
  table, verifies a strict prefix match and checksums, executes pending SQL steps,
  and inserts corresponding history records using bound parameters before commit.
- SQLite `BEGIN IMMEDIATE` locks the file for cooperating writers. PostgreSQL
  `pg_advisory_xact_lock(74129461020)` serializes cooperating runners within the
  same database. The advisory lock is released on transaction end.
- `rollback_last` checks the same history integrity, executes explicit down steps
  in listed order, removes one metadata record, and commits atomically.
- No arbitrary user SQL: migration steps are trusted application code. Non-
  transactional DDL and transaction-control commands are unsupported.
- FNV-1a 64-bit checksums provide drift detection, **not** security against active
  tampering. Manual schema edits without changing recorded migrations are not
  automatically detected.
