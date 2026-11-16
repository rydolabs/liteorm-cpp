cmake_minimum_required(VERSION 3.10)
project(liteorm_cpp VERSION 0.8.0 LANGUAGES CXX)

# Verify every target in a real -std=c++11 mode, not the compiler default.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Accept previous cache option when upgrading an existing build.
if(DEFINED TINYORM_WITH_POSTGRESQL AND NOT DEFINED LITEORM_WITH_POSTGRESQL)
    set(LITEORM_WITH_POSTGRESQL "${TINYORM_WITH_POSTGRESQL}" CACHE BOOL "Build PostgreSQL backend using libpq")
endif()
option(LITEORM_WITH_POSTGRESQL "Build PostgreSQL backend using libpq" ON)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(liteorm STATIC src/database.cpp src/sqlite.cpp src/migration.cpp)
target_include_directories(liteorm PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
add_library(liteorm::liteorm ALIAS liteorm)
add_library(tinyorm::tinyorm ALIAS liteorm)
target_compile_features(liteorm PUBLIC cxx_std_11)
set_target_properties(liteorm PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
if(TARGET SQLite::SQLite3)
    target_link_libraries(liteorm PUBLIC SQLite::SQLite3)
else()
    target_include_directories(liteorm PRIVATE "${SQLite3_INCLUDE_DIRS}")
    target_link_libraries(liteorm PUBLIC ${SQLite3_LIBRARIES})
endif()

if(LITEORM_WITH_POSTGRESQL)
    find_package(PostgreSQL REQUIRED)
    target_sources(liteorm PRIVATE src/postgresql.cpp)
    if(TARGET PostgreSQL::PostgreSQL)
        target_link_libraries(liteorm PUBLIC PostgreSQL::PostgreSQL)
    else()
        target_include_directories(liteorm PRIVATE ${PostgreSQL_INCLUDE_DIRS})
        target_link_libraries(liteorm PUBLIC ${PostgreSQL_LIBRARIES})
    endif()
    target_compile_definitions(liteorm PUBLIC LITEORM_HAS_POSTGRESQL=1 TINYORM_HAS_POSTGRESQL=1)
else()
    target_sources(liteorm PRIVATE src/postgresql_disabled.cpp)
    target_compile_definitions(liteorm PUBLIC LITEORM_HAS_POSTGRESQL=0 TINYORM_HAS_POSTGRESQL=0)
endif()

# Deprecated in-tree target alias for old builds.
add_library(tinyorm_sqlite ALIAS liteorm)
add_library(liteorm_sqlite ALIAS liteorm)

# CMake consumers can use find_package(liteorm_cpp CONFIG REQUIRED) and
# target_link_libraries(app PRIVATE liteorm::liteorm).
set(LITEORM_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/liteorm_cpp")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/liteorm_cppConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/liteorm_cppConfig.cmake"
    INSTALL_DESTINATION "${LITEORM_INSTALL_CMAKEDIR}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/liteorm_cppConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(TARGETS liteorm EXPORT liteorm_cppTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/liteorm"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/tinyorm"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT liteorm_cppTargets FILE liteorm_cppTargets.cmake
    NAMESPACE liteorm:: DESTINATION "${LITEORM_INSTALL_CMAKEDIR}")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/liteorm_cppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/liteorm_cppConfigVersion.cmake"
    DESTINATION "${LITEORM_INSTALL_CMAKEDIR}")
# Legacy find_package(tinyorm_cpp11) compatibility.
set(TINYORM_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/tinyorm_cpp11")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tinyorm_cpp11Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tinyorm_cpp11Config.cmake"
    INSTALL_DESTINATION "${TINYORM_INSTALL_CMAKEDIR}")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tinyorm_cpp11ConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tinyorm_cpp11Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tinyorm_cpp11ConfigVersion.cmake"
    DESTINATION "${TINYORM_INSTALL_CMAKEDIR}")

add_executable(liteorm_repository_demo examples/repository_main.cpp)
target_link_libraries(liteorm_repository_demo PRIVATE liteorm)
add_executable(liteorm_demo examples/main.cpp)
target_link_libraries(liteorm_demo PRIVATE liteorm)
add_executable(liteorm_migrations_demo examples/migrations_main.cpp)
target_link_libraries(liteorm_migrations_demo PRIVATE liteorm)
add_executable(liteorm_relations_demo examples/relations_main.cpp)
target_link_libraries(liteorm_relations_demo PRIVATE liteorm)
add_executable(liteorm_migration_concurrency_tests tests/test_migration_concurrency.cpp)
target_link_libraries(liteorm_migration_concurrency_tests PRIVATE liteorm Threads::Threads)
add_executable(liteorm_migration_tests tests/test_migrations.cpp)
target_link_libraries(liteorm_migration_tests PRIVATE liteorm)
add_executable(liteorm_core_basics_tests tests/test_core_basics.cpp)
target_link_libraries(liteorm_core_basics_tests PRIVATE liteorm)
add_executable(liteorm_core_safety_tests tests/test_core_safety.cpp)
target_link_libraries(liteorm_core_safety_tests PRIVATE liteorm)
add_executable(liteorm_portable_core_tests tests/test_portable_core.cpp)
target_link_libraries(liteorm_portable_core_tests PRIVATE liteorm)
add_executable(liteorm_repository_tests tests/test_repository.cpp)
target_link_libraries(liteorm_repository_tests PRIVATE liteorm)
add_executable(liteorm_tests tests/test_sqlite.cpp)
target_link_libraries(liteorm_tests PRIVATE liteorm)
add_executable(liteorm_join_tests tests/test_join.cpp)
target_link_libraries(liteorm_join_tests PRIVATE liteorm)
add_executable(liteorm_dialect_tests tests/test_dialects.cpp)
target_link_libraries(liteorm_dialect_tests PRIVATE liteorm)
add_executable(liteorm_relation_tests tests/test_relations.cpp)
target_link_libraries(liteorm_relation_tests PRIVATE liteorm)

if(LITEORM_WITH_POSTGRESQL)
    add_executable(liteorm_pg_demo examples/postgresql_main.cpp)
    target_link_libraries(liteorm_pg_demo PRIVATE liteorm)
    add_executable(liteorm_postgresql_tests tests/test_postgresql.cpp)
    target_link_libraries(liteorm_postgresql_tests PRIVATE liteorm)
    add_executable(liteorm_postgresql_migration_tests tests/test_postgresql_migrations.cpp)
    target_link_libraries(liteorm_postgresql_migration_tests PRIVATE liteorm)
endif()

# Tests rely on assert(); keep assertions enabled in Release builds, too.
foreach(test_target liteorm_repository_tests liteorm_core_basics_tests liteorm_core_safety_tests liteorm_portable_core_tests liteorm_tests liteorm_join_tests liteorm_dialect_tests liteorm_relation_tests liteorm_migration_tests liteorm_migration_concurrency_tests)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
endforeach()
if(LITEORM_WITH_POSTGRESQL AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(liteorm_postgresql_tests PRIVATE -UNDEBUG)
    target_compile_options(liteorm_postgresql_migration_tests PRIVATE -UNDEBUG)
endif()

enable_testing()
add_test(NAME sqlite_repository COMMAND liteorm_repository_tests)
add_test(NAME sqlite_migration_concurrency COMMAND liteorm_migration_concurrency_tests)
add_test(NAME sqlite_migration_integration COMMAND liteorm_migration_tests)
add_test(NAME sqlite_orm_integration COMMAND liteorm_tests)
add_test(NAME sqlite_core_basics COMMAND liteorm_core_basics_tests)
add_test(NAME sqlite_core_safety COMMAND liteorm_core_safety_tests)
add_test(NAME sqlite_portable_core COMMAND liteorm_portable_core_tests)

add_test(NAME sql_dialect_compilation COMMAND liteorm_dialect_tests)
add_test(NAME sqlite_inner_join COMMAND liteorm_join_tests)
add_test(NAME sqlite_relations_and_join_chain COMMAND liteorm_relation_tests)
if(LITEORM_WITH_POSTGRESQL)
    add_test(NAME postgresql_repository COMMAND liteorm_repository_tests --postgresql)
    set_tests_properties(postgresql_repository PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME postgresql_portable_core COMMAND liteorm_portable_core_tests --postgresql)
    set_tests_properties(postgresql_portable_core PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME postgresql_core_basics COMMAND liteorm_core_basics_tests --postgresql)
    add_test(NAME postgresql_core_safety COMMAND liteorm_core_safety_tests --postgresql)
    set_tests_properties(postgresql_core_safety PROPERTIES SKIP_RETURN_CODE 77)
    set_tests_properties(postgresql_core_basics PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME postgresql_orm_integration COMMAND liteorm_postgresql_tests)
    set_tests_properties(postgresql_orm_integration PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME postgresql_migration_integration COMMAND liteorm_postgresql_migration_tests)
    set_tests_properties(postgresql_migration_integration PROPERTIES SKIP_RETURN_CODE 77)
endif()
