#include "liteorm/liteorm.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

int main() {
    // A distinct on-disk SQLite database is required to exercise writer locking.
    const std::string path = "liteorm_migration_lock_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite3";
    std::remove(path.c_str());
    std::mutex mutex;
    std::condition_variable condition;
    bool start = false;
    int ready = 0;
    int applied = 0;
    int skipped = 0;
    std::exception_ptr first_error;

    const auto work = [&]() {
        try {
            liteorm::Database db(path);
            liteorm::MigrationRunner runner(db);
            std::vector<liteorm::Migration> versions;
            versions.push_back(liteorm::Migration("0001_parallel")
                .up("CREATE TABLE parallel_test (id INTEGER PRIMARY KEY)")
                .down("DROP TABLE parallel_test"));
            {
                std::unique_lock<std::mutex> lock(mutex);
                ++ready;
                condition.notify_all();
                condition.wait(lock, [&]() { return start; });
            }
            const liteorm::MigrationReport report = runner.apply_all(versions);
            std::lock_guard<std::mutex> lock(mutex);
            applied += static_cast<int>(report.applied.size());
            skipped += static_cast<int>(report.skipped.size());
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!first_error) first_error = std::current_exception();
            condition.notify_all();
        }
    };
    std::thread a(work);
    std::thread b(work);
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&]() { return ready == 2 || first_error; });
        start = true;
        condition.notify_all();
    }
    a.join();
    b.join();
    if (first_error) { std::remove(path.c_str()); std::rethrow_exception(first_error); }
    assert(applied == 1 && skipped == 1);
    {
        liteorm::Database db(path);
        liteorm::MigrationRunner runner(db);
        assert(runner.applied().size() == 1);
    }
    assert(std::remove(path.c_str()) == 0);
    std::cout << "Concurrent SQLite migration serialization passed\n";
}
