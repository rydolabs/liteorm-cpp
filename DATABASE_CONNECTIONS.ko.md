# LiteORM-cpp: SQLite 및 PostgreSQL 사용 예제 (C++11)

두 데이터베이스는 연결 생성 코드만 다릅니다. Repository 기반 CRUD 코드는 같습니다.

## SQLite

SQLite 개발 파일이 설치되어 있으면 별도의 DB 서버 없이 파일로 실행됩니다.

```cpp
auto db = liteorm::Database::sqlite("app.db");    // 현재 작업 디렉터리에 파일 생성
// auto db = liteorm::Database::sqlite(":memory:"); // 종료 시 삭제되는 메모리 DB
```

실행 가능한 소스: [examples/sqlite_object_main.cpp](examples/sqlite_object_main.cpp)

## PostgreSQL

PostgreSQL 서버와 libpq 개발 패키지가 필요합니다. PostgreSQL 사용 시 백엔드를 명시합니다.

```cpp
const char* dsn = std::getenv("LITEORM_PG_DSN");
if (!dsn || !*dsn) throw std::runtime_error("LITEORM_PG_DSN not set");
auto db = liteorm::Database::postgresql(dsn);
```

테스트용 DB 준비 후 실행:

```bash
export LITEORM_PG_DSN='host=127.0.0.1 port=5432 dbname=liteorm_demo user=postgres connect_timeout=5'
# 비밀번호는 libpq의 PGPASSWORD 환경변수 또는 .pgpass 등에 별도 설정
./build/liteorm_postgresql_object_example
```

실행 가능한 소스: [examples/postgresql_object_main.cpp](examples/postgresql_object_main.cpp)

기존 `Database("app.db")` 및 `Database(conninfo, BackendKind::PostgreSQL)`
생성자도 하위 호환용으로 유지합니다. PostgreSQL 비활성화 빌드에서는
`Database::postgresql()` 호출 시 예외가 발생합니다.

팩토리는 C++11 이동 생성자를 통해 `Database` 객체를 반환합니다.
Repository나 Transaction을 만든 **후에는 Database를 이동하지 마세요.**

## 공통 ORM API

```cpp
struct User {
    std::int64_t id;
    std::string name;
    int age;
    User() : id(0), age(0) {}
};

db.model<User>("liteorm_demo_users")
  .primary_key("id", &User::id)
  .field("name", &User::name)
  .field("age", &User::age);
auto users = db.repository<User>();
users.create_table();
User user;
user.name = "Alice";
user.age = 25;
users.create(user);
auto found = users.find(user.id);
auto adults = users.where(liteorm::col(&User::age) >= 18).all();
users.update(user);
users.remove(user.id);
```

## 빌드

```bash
# SQLite + PostgreSQL
cmake -S . -B build -DLITEORM_WITH_POSTGRESQL=ON
cmake --build build -j 2
./build/liteorm_sqlite_object_example
./build/liteorm_postgresql_object_example   # LITEORM_PG_DSN 필요

# SQLite 전용
cmake -S . -B build-sqlite -DLITEORM_WITH_POSTGRESQL=OFF
cmake --build build-sqlite -j 2
./build-sqlite/liteorm_sqlite_object_example
```

예제는 liteorm_demo_users 테이블을 생성하고 삽입한 행은 삭제하지만 테이블은 남깁니다.
SQLite 예제는 작업 디렉터리에 liteorm_demo.sqlite3 파일도 생성합니다.
PostgreSQL 예제는 쓰기 가능한 테스트 DB에서만 실행하세요.
실서비스의 테이블 스키마는 검토된 Migration으로 관리하는 것을 권장합니다.
