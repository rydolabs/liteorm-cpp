#include "liteorm/liteorm.hpp"
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

struct Account {
    std::int64_t id;
    std::string email;
    Account() : id(0) {}
};
struct Accounts : liteorm::Table<Account> {
    liteorm::Column<Account, std::int64_t> id;
    liteorm::Column<Account, std::string> email;
    Accounts() : Table("demo_accounts"),
        id(add("id", &Account::id, liteorm::ColumnOptions::identity())),
        email(add("email", &Account::email, liteorm::ColumnOptions::unique_key())) {}
};

int main() {
    liteorm::Database db(":memory:");
    Accounts accounts;
    std::vector<liteorm::Migration> migrations;

    liteorm::Migration create("20261115_0001_create_accounts");
    create.sqlite_up(liteorm::create_table_sql(accounts, liteorm::SqlDialect::SQLite))
          .postgresql_up(liteorm::create_table_sql(accounts, liteorm::SqlDialect::PostgreSQL))
          .down("DROP TABLE demo_accounts");
    migrations.push_back(create);

    liteorm::Migration add_index("20261115_0002_email_lookup");
    add_index.up("CREATE INDEX demo_accounts_email_idx ON demo_accounts(email)")
             .down("DROP INDEX demo_accounts_email_idx");
    migrations.push_back(add_index);

    liteorm::MigrationRunner runner(db);
    std::cout << "Pending migrations: " << runner.pending(migrations).size() << "\n";
    const liteorm::MigrationReport result = runner.apply_all(migrations);
    std::cout << "Applied: " << result.applied.size() << ", already applied: "
              << result.skipped.size() << "\n";
    Account alice;
    alice.email = "alice@example.org";
    const std::int64_t id = db.insert(accounts, alice);
    std::cout << "Inserted row ID: " << id << "\n";
    std::cout << "History entries: " << runner.applied().size() << "\n";
    return 0;
}
