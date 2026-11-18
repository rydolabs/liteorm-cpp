# LiteORM-cpp v0.8.1 API 안정화

## create() 반환값

`repository.create(object)`는 `void`를 반환합니다. 성공 여부는 반환 ID가 아닌 예외로 확인합니다. 자동 생성 정수 기본 키는 성공 시 객체의 `id` 필드에 반영됩니다. 문자열 등 수동 기본 키는 그대로 유지됩니다.

## 모델 등록 실패

`db.model<T>()`의 연쇄 호출 중 유효하지 않은 컬럼/기본 키가 검출되면 해당 등록은 사용 불가능합니다. `repository<T>()`는 미완료 등록에 대해 `std::logic_error`를 발생시키고, 같은 모델을 다시 등록할 수 있습니다. 등록 완료는 ModelBuilder의 마지막 복사본이 소멸할 때입니다. 보통은 한 문장으로 모델 등록을 마치세요.

## Database 수명

DB가 파괴된 다음 Repository/RepositoryQuery를 사용하면 `std::logic_error`가 발생합니다. 해당 핸들, Statement, Transaction이 살아 있는 동안 `Database` 이동 생성도 거부합니다. 기존 Table/SQL 기반 API의 모든 핸들에 똑같은 보장이 적용되는 것은 아닙니다. Database를 다른 스레드에서 동시에 파괴하거나 공유하는 것은 지원하지 않습니다.

## 검증

`sqlite_repository_safety` 테스트가 사용 중 이동, 파괴된 DB 접근, 부분 등록 실패 및 재등록, 수동 문자열 키를 검사합니다. PostgreSQL 17 실서버 테스트는 GitHub Actions에서 기존 통합 테스트와 함께 수행됩니다.


## 확정된 API 계약

`Repository<T>::create(T&)`는 **반환형 `void`로 확정**합니다.
이전 v0.8 프리뷰의 반환값을 기대하는 코드는 변경해야 합니다.
저수준 `Database::insert(table, object)`는 호환성을 위해 변경하지 않습니다.
자세한 규칙과 마이그레이션: [CREATE_API_CONTRACT.ko.md](CREATE_API_CONTRACT.ko.md).
