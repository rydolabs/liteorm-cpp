# LiteORM-cpp Object-first API (C++11)

Register a model once with db.model<T>(table).primary_key(...).field(...).
Then obtain auto-provided db.repository<T>() and use create, find, find_by,
find_all, update, remove, count and exists without writing SQL.

find(id) returns Optional<T>; an absent row has !has_value().
create(T&) executes immediately and assigns generated integer primary keys.
For manually assigned or string keys: primary_key("id", &T::id, false).
update(const T&) rewrites all mapped non-key fields, without change tracking.
remove(id) deletes by primary key and returns affected-row count.
find_by(member, value) returns a vector of matching objects.

Complex conditions use liteorm::col(&T::field) in repository.where(...).
AND, OR, NOT, comparisons, in(), is_null() and is_not_null() are supported.
order_by(&T::field), order_by_desc(&T::field), limit(n), all(), first() are optional.

Repository handles must not outlive the Database. Database is not thread-safe.
create_table() is intended for demos; use reviewed migrations for production.
Legacy Table/Column and SQL-oriented API remain supported, but are not the default.
Connection pool, change tracking and automatic schema diff are out of scope.

SQLite/PostgreSQL 연결 및 실행 예제: [DATABASE_CONNECTIONS.ko.md](DATABASE_CONNECTIONS.ko.md)
