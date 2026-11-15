# LiteORM-cpp v0.7-core 검증 기록

2026-11-16 기준 현재 실행 환경에서 확인한 결과입니다. PostgreSQL 서버에 접속한 결과를 주장하지 않습니다.

## 도구 및 의존성

- GCC 14.2, Clang 17, CMake 3.31, SQLite 3.46, libpq 17.11
- 컴파일 표준: **C++11**, `-Wall -Wextra -Werror -pedantic`
- CMake 옵션: `LITEORM_WITH_POSTGRESQL=ON/OFF` 각각 검증

## 결과

| 구성 | 로컬 실행 결과 |
| --- | --- |
| GCC + libpq 활성화 | C++11 빌드 성공, 14개 테스트 중 9개 통과 · PostgreSQL 서버 테스트 5개 Skipped |
| Clang + libpq 활성화 | C++11 빌드 성공, 14개 테스트 중 9개 통과 · PostgreSQL 서버 테스트 5개 Skipped |
| GCC + SQLite 전용 (`OFF`) | C++11 빌드 성공, 9개 테스트 모두 통과 |
| 외부 CMake `find_package` | libpq 활성화 / SQLite 전용 각각 설치·컴파일·실행 성공 |
| Quickstart C++ 예제 | `g++ -std=c++11 -Wall -Wextra -Werror -pedantic` 컴파일·실행 성공 |
| AddressSanitizer + UndefinedBehaviorSanitizer | SQLite의 `liteorm_portable_core_tests`, `liteorm_tests`, `liteorm_core_basics_tests`, `liteorm_core_safety_tests` 실행 통과 |
| PostgreSQL 17 실서버 연결·동작 | 이 환경에서는 서버가 없어 **미실행** |

## 재현 명령

```bash
cmake -S . -B build -DLITEORM_WITH_POSTGRESQL=ON \
  -DCMAKE_CXX_STANDARD=11 \
  -DCMAKE_CXX_FLAGS='-Wall -Wextra -Werror -pedantic'
cmake --build build -j 2
ctest --test-dir build --output-on-failure

# PostgreSQL 필수 검증 (전용 테스트 DB를 연결한 환경에서)
export LITEORM_TEST_PG_DSN='host=127.0.0.1 port=5432 user=... password=... dbname=... connect_timeout=5'
export LITEORM_REQUIRE_PG=1
ctest --test-dir build --output-on-failure
```

서버 테스트를 필수 조건으로 삼으려면 `LITEORM_REQUIRE_PG=1`을 사용하세요. DB를 만들고 삭제하는 테스트이므로 운영 DB에서 실행하지 마세요. CMake/CI 포함은 실제 CI가 성공했다는 뜻이 아닙니다.
