# LiteORM-cpp v0.8.1: Repository::create() API 규칙

**확정:** C++11 Object-first API의 `Repository<T>::create(T&)` 반환형은 `void`입니다.

```cpp
User alice;
alice.name = "Alice";
users.create(alice);          // void: 삽입 성공 시 정상 반환
std::int64_t id = alice.id;   // 자동 생성된 정수 기본 키
```

- DB INSERT 성공: 예외 없이 반환합니다. 자동 생성 정수 기본 키가 있다면 해당 멤버에 값을 기록합니다.
- 수동 문자열/정수 기본 키: `primary_key(..., false)`로 매핑하고 사용자 지정 값을 그대로 유지합니다.
- INSERT 실패: DB 드라이버의 예외를 전달합니다. 실패를 `0`, `-1`, `false` 등의 반환값으로 표현하지 않습니다.
- 기존 객체의 모든 비기본키 필드 값은 `update(model)` 호출 시 저장됩니다. `create()`에 change tracking은 없습니다.
- 대량 삽입·관계 연쇄 저장·자동 재시도는 포함하지 않습니다.

### 기존 코드 마이그레이션

**기존 v0.8 초기 프리뷰 (더 이상 지원하지 않음):**

```cpp
auto id = users.create(alice);
```

**v0.8.1 이후:**

```cpp
users.create(alice);
auto id = alice.id;
```

기존 Table/SQL 기반의 저수준 `Database::insert(table, object)`는 반환형 `std::int64_t`를 유지합니다.
**Repository의 `create()`와 저수준 `insert()`는 별개의 API**입니다.

### 테스트 정책

- GCC/Clang C++11 컴파일 시 `create()`의 반환형이 `void`인지 `static_assert`로 검사합니다.
- SQLite 및 PostgreSQL에서 자동 생성 정수 키와 수동 문자열 키 삽입을 테스트합니다.
- 수동 문자열 키 중복 삽입은 예외를 내야 하고, DB에는 기존 행만 남아야 합니다.

이 규칙은 v1.0 안정화 전까지 공개 Object-first API의 변경 기준으로 사용합니다.
