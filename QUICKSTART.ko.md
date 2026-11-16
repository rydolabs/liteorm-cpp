# LiteORM-cpp C++11 빠른 시작 (v0.8)

C++11, SQLite3을 필수로 사용합니다. PostgreSQL은 `-DLITEORM_WITH_POSTGRESQL=ON`으로 libpq 드라이버를 포함합니다. DB 연결 풀/Change Tracking은 없습니다.

## 1. 빌드

```bash
cmake -S . -B build -DLITEORM_WITH_POSTGRESQL=ON \
  -DCMAKE_CXX_STANDARD=11 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_CXX_EXTENSIONS=OFF
cmake --build build -j
ctest --test-dir build --output-on-failure
```

PostgreSQL을 사용하지 않으면 `-DLITEORM_WITH_POSTGRESQL=OFF`로 빌드합니다. libpq 개발 파일이 필요 없습니다.

## 2. 권장 객체 기반 API

새 모델 등록과 CRUD는 [OBJECT_API.ko.md](OBJECT_API.ko.md)를 참고하세요.
기존 Table/Column API는 계속 지원합니다.

## 3. 기존 모델 정의 및 CRUD

```cpp
#include "liteorm/liteorm.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
    liteorm::Database db(":memory:"); // SQLite
    Users users;
    db.create_table(users);

    User alice;
    alice.name = "Alice";
    alice.age = 25;
    const std::int64_t id = db.insert(users, alice);

    const std::vector<User> adults = db.select(users)
        .where(users.age >= 18)
        .order_by(users.name.asc())
        .limit(10).all();

    std::unique_ptr<User> found = db.find(users, users.id, id);
    if (!found) return 1;

    db.update(users).set(users.age, 26)
        .where(users.id == id).execute();

    {
        liteorm::Transaction tx = db.transaction();
        db.remove(users).where(users.id == id).execute();
        tx.commit();
    }
    return db.select(users).count() == 0 && adults.size() == 1 ? 0 : 1;
}
```

PostgreSQL에서는 `Database` 생성 줄만 바꿉니다. 사전 생성한 DB에 접속할 사용자·암호·권한을 제공해야 합니다.

```cpp
// libpq 사용이 활성화된 빌드
// liteorm::Database db("host=127.0.0.1 port=5432 dbname=mydb user=myuser ...",
//                      liteorm::BackendKind::PostgreSQL);
```

`Table` 및 `Column` 객체는 이를 사용하는 쿼리 객체보다 오래 살아 있어야 합니다. `find()`는 `std::unique_ptr`을 반환하며, 행이 없으면 빈 포인터가 됩니다. `insert()`는 정수 기본 키의 값만 반환합니다. 모든 행을 수정/삭제하려면 `.all_rows()`를 명시해야 합니다.

## 3. 외부 CMake 프로젝트 사용

```bash
cmake --install build --prefix "$PWD/stage"
```

```cmake
find_package(liteorm_cpp11 0.7 CONFIG REQUIRED)
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE liteorm::liteorm)
```

외부 앱 구성 시 `-DCMAKE_PREFIX_PATH=/path/to/stage`를 지정합니다. 설치된 PostgreSQL 빌드는 libpq 개발/링크 환경도 필요합니다.

## 4. 오류 및 안전성

- 값은 WHERE/INSERT/UPDATE에서 Prepared Statement로 바인딩합니다. 사용자 입력으로 SQL 문자열을 직접 연결하지 않습니다.
- `execute_sql()`과 Migration SQL은 **검토된 신뢰 코드 전용**입니다.
- 오류는 `liteorm::SqliteError`(SQLite 오류 코드), `liteorm::PostgresError`(SQLSTATE) 또는 타입/사용 오류 예외로 전달됩니다.
- 미완료 트랜잭션은 소멸 시 롤백을 시도합니다. `Database`를 여러 스레드에서 동시에 사용하지 않습니다.
- SQLite는 TEXT 안의 NUL 바이트를 보존하지만 PostgreSQL TEXT는 거부합니다. 바이너리 자료형은 현재 지원하지 않습니다.
- PostgreSQL 실서버 통합 테스트는 `LITEORM_TEST_PG_DSN`와 테스트 전용 DB를 이용합니다. 자세한 설정은 [POSTGRESQL_TESTING.ko.md](POSTGRESQL_TESTING.ko.md)에서 확인하세요.
