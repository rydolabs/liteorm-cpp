# v0.8 — Object-first Repository API

- Added model<T>() and repository<T>() for simple object CRUD.
- Added typed col(&T::field) predicates and chained queries.
- Generated integer primary keys are copied back into inserted objects.
- Preserved legacy Table/Column/SQL APIs and C++11 compatibility.
- Added SQLite and PostgreSQL 17 repository integration coverage and demo.
- No pool, change tracking, automatic schema diff, or asynchronous API.

---

# v0.7-core — 기본 CRUD 호환성·문서·회귀 테스트 강화

- 프로젝트명 **LiteORM-cpp** 유지; CMake 패키지 버전을 **0.7.0**으로 갱신. C++11 유지.
- `insert_plan()`에 빈 모델 매핑 방어 검사 추가.
- 새 `sqlite_portable_core` / `postgresql_portable_core` 테스트: SQL 바인딩, UNIQUE, NULL, UTF-8, 빈 문자열, 트랜잭션 이동/롤백, 잘못된 Dialect, DB별 TEXT NUL 바이트 차이.
- `tests/consumer` 의 요구 패키지 버전을 0.7로 업데이트.
- `QUICKSTART.ko.md` 추가. PostgreSQL Docker 가이드 임시 DB 비밀번호 누락 수정.
- 기능 추가 범위를 늘리지 않음: Connection Pool, Session, 자동 Migration Diff, 새 SQL DSL 미구현.
- PostgreSQL 실서버 테스트는 실행 환경 제한으로 **미실행**.

---

# v0.6-core — 기본 ORM 안정성 및 PostgreSQL 필수 테스트 준비

- PostgreSQL 정수형 매핑 수정: `uint16_t` → `INTEGER`, `uint32_t` → `BIGINT`. unsigned `uint64_t` 최대값은 DB 한계 초과 시 예외.
- 부동소수점 역직렬화에서 `float` 표현 범위 초과 시 `std::out_of_range` 처리.
- SQLite 변경용 `execute()`에서 읽기 전용 쿼리 거부: 빈 SELECT가 직전 DML 변경 건수를 보고하던 문제 수정.
- SQLite/PostgreSQL 공통 숫자 경계값·Prepared Statement·제약 위반·실패 후 트랜잭션 롤백 테스트 추가.
- PostgreSQL 테스트 엄격 모드(`LITEORM_REQUIRE_PG=1`)와 CI 구성 보완, SQLite 전용 CI 작업 추가.
- 설치 패키지 버전 0.6.0 및 외부 CMake consumer 테스트 업데이트.
- PostgreSQL 서버가 없는 로컬 환경에서는 PostgreSQL 서버 통합 테스트를 수행하지 못함. 실행 가이드: [POSTGRESQL_TESTING.ko.md](POSTGRESQL_TESTING.ko.md).

# v0.5-core — 기본 기능 우선 / 안정화

- v1.0 필수 범위 축소 및 우선순위 고정: [BASIC_SCOPE.ko.md](BASIC_SCOPE.ko.md).
- 단일 테이블 기본 조회에 `count()`와 `exists()` 추가 (`WHERE`만 적용, 정렬/제한 무시).
- `insert()`의 ID 반환 의미 수정: 수동 지정 정수 기본 키 반환; 기본 키가 없거나 문자열 키면 `0`.
- SQLite 파일 데이터 재연결 테스트, 정수 키·문자열 키·키 없는 테이블·집계·트랜잭션 회귀 테스트 추가.
- CMake 설치/Export: 외부 프로젝트에서 `find_package(liteorm_cpp11 CONFIG REQUIRED)` 및 `liteorm::liteorm` 사용 가능.
- CI 설치 소비자 스모크 테스트 추가.
- PostgreSQL 실제 서버 테스트는 현재 로컬 환경에서 미실행 (CI/환경변수 기반 테스트는 구성됨).

---

# Changelog

## v0.4.0 — explicit SQL migrations (C++11)

- Added `Migration` / `MigrationRunner` with explicit, ordered up/down SQL; common and backend-specific overrides.
- Added `_liteorm_migrations` history table, stable source fingerprint, strict applied-prefix validation, pending/status, idempotent apply, and last-migration rollback.
- Pending migrations plus history are applied in one transaction; SQLite `BEGIN IMMEDIATE` and PostgreSQL `pg_advisory_xact_lock` serialize cooperating runners.
- Added `create_table_sql(model, dialect)` to generate DDL without running it, useful when constructing migrations.
- Added SQLite migration + two-connection concurrency integration tests, PostgreSQL live integration test (skipped without DSN), and a migration example.
- No automatic schema comparison, migration file discovery or CLI in this version.

## v0.3.0 — relational queries (C++11)

- New `Database::left_join()` with absent right-side rows mapped as `Optional<T>`.
- New chainable `join()`/`left_join()` for three or more tables; results as `std::tuple`.
- New typed `has_many()` and `belongs_to()` relation descriptors.
- New `Database::load_many()` batching up to 500 distinct parent keys per query,
  grouping related children by parent position (including duplicates).
- New bound `Column::in(std::vector<Field>)` with safe empty-list behavior.
- New SQLite integration tests for outer/multi joins and relationships; expanded PG
  live tests to cover them when a PostgreSQL test instance is configured.
- New `liteorm_relations_demo`.

The public two-table `db.join(...).on(...).all()` **row shape** remains `pair<A,B>`.
The concrete bound query *type* returned by `db.join()` changed to a generalized
executor; code depending directly on the previous `BoundJoin<A,B>` type may need
updating. C++11 remains the only required language standard.

## v0.2.0 — dual database backends

SQLite and PostgreSQL/libpq, common CRUD and transaction API, FK DDL, and INNER JOIN.
