# TinyORM → LiteORM-cpp migration

The migration keeps ORM behavior and C++11 support unchanged; naming and packaging are updated.

| Old | New (preferred) |
|---|---|
| `#include <tinyorm/tinyorm.hpp>` | `#include <liteorm/liteorm.hpp>` |
| `tinyorm::` | `liteorm::` |
| `find_package(tinyorm_cpp11 CONFIG REQUIRED)` | `find_package(liteorm_cpp CONFIG REQUIRED)` |
| `tinyorm::tinyorm` CMake target | `liteorm::liteorm` |
| `TINYORM_WITH_POSTGRESQL` | `LITEORM_WITH_POSTGRESQL` |
| `TINYORM_TEST_PG_DSN` | `LITEORM_TEST_PG_DSN` |
| `TINYORM_REQUIRE_PG` | `LITEORM_REQUIRE_PG` |

**Compatibility:** old public header paths, namespace alias, CMake package/target and PostgreSQL environment/config variables still work. Old artifacts remain in `docs/history`.

**API scope:** CRUD, typed queries, joins, explicit migrations and RAII transactions. No new connection pool, session or automatic schema-diff functionality is introduced.

**Deployment:** a project previously linked against a compiled `tinyorm` binary must rebuild after this rename; only source-level compatibility is offered, *not* a guarantee of binary/ABI compatibility. SQL database schema and migration history table names have not been changed.
