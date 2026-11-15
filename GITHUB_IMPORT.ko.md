# GitHub 저장소 이관 가이드

대상: https://github.com/rydolabs/liteorm-cpp (확인 시점에는 빈 저장소)

## 1. 파일 배포

릴리스 ZIP을 압축 해제한 뒤 `liteorm-cpp` 폴더에서 진행합니다. 새 저장소이므로 기존 TinyORM 기록을 병합하지 않고 v0.7-core 스냅샷을 초기 커밋으로 이관합니다.

```bash
unzip liteorm_cpp_v0.7_migration.zip
cd liteorm-cpp

git init -b main
git add .
git commit -m "Initial import: LiteORM-cpp (TinyORM v0.7-core migration)"
git remote add origin https://github.com/rydolabs/liteorm-cpp.git
git push -u origin main
```

HTTPS 인증에 GitHub 계정 인증 또는 PAT가 필요할 수 있습니다. 이미 Git 환경에 `origin`이 설정되어 있다면 `git remote add` 대신 `git remote set-url origin ...`를 사용합니다.

## 2. 빌드 검사

```bash
cmake -S . -B build -DLITEORM_WITH_POSTGRESQL=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

PostgreSQL 서버 테스트는 `LITEORM_TEST_PG_DSN` 환경변수를 실제 테스트용 DB에 지정해야 실행됩니다. CI에서는 PostgreSQL 서비스로 검증하도록 워크플로가 포함되어 있습니다. **로컬 테스트 결과만으로 PostgreSQL 실서버 성공을 선언하지 않습니다.**

## 3. API 마이그레이션

- 권장 사용: `#include <liteorm/liteorm.hpp>`, `liteorm::Database`, CMake `liteorm::liteorm`.
- 기존 사용자 호환: `#include <tinyorm/tinyorm.hpp>`, `tinyorm::Database`, CMake `tinyorm::tinyorm`.
- 기존 바이너리(`libtinyorm.a`)와의 ABI 호환성은 보장하지 않습니다. 새 이름으로 전체 프로젝트를 재빌드하세요.
- 기존 데이터베이스와 `_tinyorm_migrations` 이력 테이블은 건드리지 않습니다.

## 4. 공개 전 확인

- 라이선스 정책을 결정한 뒤 `LICENSE` 추가(기본 패키지에는 임의로 라이선스를 넣지 않음)
- GitHub Actions CI의 PostgreSQL 실서버 결과 확인
- README 예제 및 설치 방식이 지원 플랫폼에서 정상 작동하는지 확인
