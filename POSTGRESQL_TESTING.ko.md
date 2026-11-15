# PostgreSQL 실제 서버 테스트 가이드

**중요:** `libpq` 컴파일/링크와 SQL 생성 테스트만으로는 PostgreSQL 연동이 검증된 것이 아닙니다.
테스트할 때는 데이터 생성·삭제 권한이 있는 **전용 테스트 DB**를 사용하세요. CI는 PostgreSQL 17 서비스를 사용하도록 구성되어 있습니다.

## 로컬 Docker 예제 (Docker가 설치된 경우)

```bash
docker run -d --rm --name liteorm-test-pg \
  -e POSTGRES_USER=liteorm -e POSTGRES_PASSWORD=liteorm \
  -e POSTGRES_DB=liteorm_test -p 15432:5432 postgres:17

# PostgreSQL 서버가 연결을 받을 준비가 된 뒤 실행합니다.
export LITEORM_TEST_PG_DSN='host=127.0.0.1 port=15432 dbname=liteorm_test user=liteorm password=liteorm connect_timeout=5'
export LITEORM_REQUIRE_PG=1
cmake -S . -B build -DLITEORM_WITH_POSTGRESQL=ON -DCMAKE_CXX_STANDARD=11
cmake --build build -j
ctest --test-dir build --output-on-failure

docker stop liteorm-test-pg
```

Docker 비밀번호는 임시 로컬 테스트용 예제이며 실제 사용 시 별도 자격 증명으로 변경하세요. 테스트는 테이블·스키마를 생성/삭제하므로 **운영 DB에 실행하면 안 됩니다**.

## 테스트 분류

- `postgresql_portable_core`: 공통 쿼리 API, UTF-8, 빈 문자열, NULL, 잘못된 Dialect, 이동한 트랜잭션 롤백, UNIQUE 오류 후 재사용, TEXT NUL 거부

- `postgresql_core_basics`: 기본 CRUD, `count()`/`exists()`, 숫자·문자열·NULL 조회
- `postgresql_core_safety`: 부호 없는 정수 범위, 타입 매핑, 제약 조건 오류, 트랜잭션 자동 롤백, 실패 후 재조회
- `postgresql_orm_integration`: JOIN, 관계 로딩, FK, INSERT RETURNING, 조건절, 트랜잭션
- `postgresql_migration_integration`: 명시적 Migration과 이력 검증 (v1.0 필수 범위 바깥)

로컬에서 DSN이 없으면 기본적으로 PostgreSQL 테스트는 종료 코드 77로 **Skipped**가 됩니다.
CI에서는 `LITEORM_REQUIRE_PG=1`을 지정하므로 DSN이 없으면 **실패**합니다. PostgreSQL 서버 연결 실패와 SQL 오류도 실패로 처리합니다.

확인하지 않은 PostgreSQL 실서버 테스트 결과를 성공으로 취급해서는 안 됩니다.
