# LiteORM-cpp 이관본 검증 결과

기준 소스: TinyORM v0.7-core.

- GCC 14.2, `-std=c++11`, `-Wall -Wextra -Werror -pedantic`: PostgreSQL libpq 활성화 빌드 성공.
- SQLite 및 SQL 생성 9개 테스트 통과.
- PostgreSQL 실서버 연동 5개 테스트: 서버/DSN 부재로 건너뜀.
- `LITEORM_WITH_POSTGRESQL=OFF` SQLite 전용 빌드 및 9개 테스트 통과.
- 설치한 CMake `liteorm_cpp` 패키지를 별도 외부 CMake 프로젝트에서 불러와 실행 성공.
- 레거시 CMake `tinyorm_cpp11` 패키지, `<tinyorm/tinyorm.hpp>`, `tinyorm::` 별칭으로 별도 외부 CMake 프로젝트 실행 성공.
- CI 워크플로는 포함됐으나 GitHub Actions 실행 결과는 확인되지 않음.

**주의:** 이번 작업은 API 명칭 및 배포 구조를 이관한 것입니다. ORM 기능을 추가하거나 PostgreSQL 서버의 실사용 적합성을 새로 검증한 작업이 아닙니다.
