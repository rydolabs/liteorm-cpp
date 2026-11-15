# LiteORM-cpp C++11 — 기본 기능 중심 개발 범위 (v0.7-core)

## 제품 목표

C++11 환경에서 SQLite와 PostgreSQL을 **동일한 모델·쿼리 API**로 다루는 가벼운 동기식 ORM 라이브러리.
복잡한 프레임워크와 실행 환경을 강제하지 않고, 애플리케이션이 DB 연결과 수명을 직접 관리한다.

## v1.0 필수 범위

1. **C++11**: C++17/20 의존성 없이 GCC·Clang 빌드, 명시적 멤버 포인터 매핑.
2. **SQLite / PostgreSQL**: 런타임별 공통 CRUD API. PostgreSQL은 빌드 옵션으로 제외 가능.
3. **모델**: 클래스/컬럼, 단일 기본 키, 자동 생성 정수 키, NULL 필드, UNIQUE, 기본 FK.
4. **기본 쿼리**: INSERT, SELECT/WHERE(AND/OR/NOT/IN), ORDER BY, LIMIT, first/find, UPDATE, DELETE, count, exists.
5. **JOIN**: INNER / LEFT JOIN 기본 기능. 추가 JOIN 기능은 기존 구현 유지, 새 확장 보류.
6. **안전성**: 값 바인딩(Prepared Statement), 식별자 이스케이프, 전체 행 UPDATE/DELETE 명시적 승인, RAII 트랜잭션.
7. **사용성/배포**: CMake 패키지 설치와 외부 소비자 `find_package` 지원, SQLite 통합 테스트 + PostgreSQL 서버 통합 테스트.

## 지금 필수는 아닌 기능 — 구현 보류

- Connection Pool / Session, Change Tracking / `save_changes()`
- 자동 스키마 Diff·생성, Migration CLI
- GROUP BY / HAVING, 서브쿼리, Projection, Self JOIN 및 Alias, OFFSET
- 비동기 API, Coroutine, Query Cache, Statement Cache, MySQL
- Composite PK, DateTime/BLOB 표준 매핑, 복잡한 객체 그래프 자동 로딩

**기존에 포함된 수동 Migration, 다중 JOIN, 관계 일괄 로딩은 삭제하지 않는다.** 그러나 새 기능의 우선순위는 안정성, 호환성, 테스트보다 낮다. Migration은 v1.0 필수 조건이 아니다.

## 현 상태 (v0.7-core)

- 구현: 모델, CRUD, WHERE, JOIN, 기본 관계 로딩, 타입 변환, 트랜잭션, 명시적 SQL Migration, `count()`, `exists()`.
- 보강: INSERT의 정수 기본 키 반환 의미 정리(자동 증가·수동 키), DB 설치용 CMake Config export, DB 파일 재연결 테스트. v0.6에서 PostgreSQL unsigned 정수 매핑과 SQLite execute()의 SELECT 판정 오류를 수정하고, 예외 복구 및 범위 검사 회귀 테스트를 추가.
- 검증: GCC 및 Clang의 C++11 엄격 경고 빌드, SQLite 통합 테스트, libpq 빌드·링크, PostgreSQL SQL Dialect 테스트, PostgreSQL 없는 구성 빌드, 외부 프로젝트 설치·링크·실행.
- **미검증**: 이번 실행 환경에서 PostgreSQL 서버 실접속 테스트. `LITEORM_TEST_PG_DSN`을 설정하면 PostgreSQL 코어/CRUD/Migration 통합 테스트를 실행하도록 구성되어 있다. CI의 PostgreSQL 서비스 테스트도 포함되어 있으나 CI 결과를 이 문서에서 주장하지 않는다.
- v0.7 변경: Portable Core 회귀 테스트 및 빈 테이블 INSERT 방어 검사. CMake 패키지 0.7.0. 자세한 재현 결과는 [VALIDATION.ko.md](VALIDATION.ko.md) 참조.
- **릴리스 성격**: MVP 프로토타입이며, 아직 운영 환경에 대한 신뢰성·성능 보증은 없다.

## 현재 검증 결과 (v0.7-core)

- 로컬 SQLite 통합/SQL 생성 테스트 9개 통과 (GCC/Clang, PostgreSQL 사용 설정).
- SQLite 전용 구성 테스트 9개 통과, 두 구성의 외부 CMake 소비자 테스트 통과.
- PostgreSQL 통합 테스트 5개는 실제 서버가 없어 건너뜀. PostgreSQL libpq 드라이버 컴파일·링크는 통과.
- `LITEORM_REQUIRE_PG=1`을 지정하면 PG DSN이 없을 때 테스트가 실패하도록 설정.

## 이후 진행 순서 (기능보다 안정성 우선)

### v0.6 — 두 DB 동작 일치 및 버그 수정 (코드·테스트 경로 구현, PG 실서버 검증은 미완료)
- 실제 PostgreSQL 서버에서 CRUD, bool/NULL/숫자 변환, 트랜잭션, JOIN, FK, 기본 조회 테스트 통과 확인
- 실패 후 같은 연결 재사용, 연결 끊김, 시간 초과 등 오류 처리 검증
- 공개 API 의미의 일관성과 문서화

### v0.7 — 기본 기능 회귀 테스트·사용 가이드 (구현 완료)
- 사용자 예제, Linux/Windows 빌드 구성 확인, 설치 소비자 테스트 CI 정례화
- 정수 범위·Unicode·SQL 주입·스키마 호환성 회귀 테스트 보강 및 DB별 TEXT 차이 문서화
- 성능 테스트는 최적화 필요성을 판단하는 용도로만 사용

### v1.0 — 기본 기능의 안정된 릴리스
- 지원 범위를 고정하고 API/오류 규약을 문서화
- 필수 SQLite 및 PostgreSQL 실서버 통합 테스트를 CI에서 통과
- CMake 설치 및 외부 소비자 프로젝트 컴파일 확인
- 추가 기능은 안정성 기준을 만족한 뒤 별도 릴리스에서 검토

## API 관련 주의

- `select(...).count()/exists()`는 `where()`의 조건만 적용하고 `order_by()`/`limit()`은 **무시**한다. 결과를 가져올 때는 `all()`/`first()`를 사용한다.
- `insert()`의 반환값은 **정수 기본 키**의 ID다. 비정수 기본 키 또는 기본 키가 없으면 `0`을 반환한다. 실제 모델의 비정수 기본 키는 `find()`로 조회할 수 있다.
- `Database`를 여러 스레드에서 동시에 사용하지 않는다. 연결 풀도 제공하지 않는다. 스레드별로 개별 연결을 사용한다.
- 원시 SQL과 Migration SQL은 **신뢰할 수 있는 코드에 한해** 사용한다. 사용자 입력은 DSL을 통해 바인딩한다.
- C++11 런타임 ABI는 컴파일러/표준 라이브러리/빌드 옵션에 따라 다르므로, 바이너리 호환성은 별도 테스트가 필요하다.
