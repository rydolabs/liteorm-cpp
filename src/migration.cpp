#include "liteorm/migration.hpp"
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace liteorm {
namespace {
const char* const history_table = "\"_liteorm_migrations\"";

void require_sql(const std::string& sql) {
    if (sql.empty() || sql.find('\0') != std::string::npos)
        throw std::invalid_argument("migration SQL must be nonempty and contain no NUL");
    bool has_nonspace = false;
    for (std::size_t i = 0; i < sql.size(); ++i) {
        if (!std::isspace(static_cast<unsigned char>(sql[i]))) { has_nonspace = true; break; }
    }
    if (!has_nonspace) throw std::invalid_argument("migration SQL may not be whitespace only");
}

void hash_byte(std::uint64_t& h, unsigned char b) {
    h ^= b;
    h *= UINT64_C(1099511628211);
}
void hash_string(std::uint64_t& h, const std::string& s) {
    std::uint64_t size = static_cast<std::uint64_t>(s.size());
    for (int i = 0; i < 8; ++i) { hash_byte(h, static_cast<unsigned char>(size & 0xffu)); size >>= 8; }
    for (std::size_t i = 0; i < s.size(); ++i)
        hash_byte(h, static_cast<unsigned char>(s[i]));
}
void hash_steps(std::uint64_t& h, const std::vector<std::string>& steps) {
    std::uint64_t n = static_cast<std::uint64_t>(steps.size());
    for (int i = 0; i < 8; ++i) { hash_byte(h, static_cast<unsigned char>(n & 0xffu)); n >>= 8; }
    for (std::size_t i = 0; i < steps.size(); ++i) hash_string(h, steps[i]);
}
} // namespace

Migration::Migration(const std::string& version) : version_(version) {
    if (version.empty() || version.size() > 128 ||
        !std::isalnum(static_cast<unsigned char>(version[0])))
        throw std::invalid_argument("migration version must start with a letter or digit and be at most 128 characters");
    for (std::size_t i = 0; i < version.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(version[i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '_' || c == '-' || c == '.'))
            throw std::invalid_argument("migration version must be ASCII alphanumeric, underscore, dash, or dot");
    }
}
Migration& Migration::up(const std::string& sql) { require_sql(sql); up_.push_back(sql); return *this; }
Migration& Migration::down(const std::string& sql) { require_sql(sql); down_.push_back(sql); return *this; }
Migration& Migration::sqlite_up(const std::string& sql) { require_sql(sql); sqlite_up_.push_back(sql); return *this; }
Migration& Migration::sqlite_down(const std::string& sql) { require_sql(sql); sqlite_down_.push_back(sql); return *this; }
Migration& Migration::postgresql_up(const std::string& sql) { require_sql(sql); pg_up_.push_back(sql); return *this; }
Migration& Migration::postgresql_down(const std::string& sql) { require_sql(sql); pg_down_.push_back(sql); return *this; }
const std::vector<std::string>& Migration::up_steps(SqlDialect d) const {
    if (d == SqlDialect::SQLite && !sqlite_up_.empty()) return sqlite_up_;
    if (d == SqlDialect::PostgreSQL && !pg_up_.empty()) return pg_up_;
    return up_;
}
const std::vector<std::string>& Migration::down_steps(SqlDialect d) const {
    if (d == SqlDialect::SQLite && !sqlite_down_.empty()) return sqlite_down_;
    if (d == SqlDialect::PostgreSQL && !pg_down_.empty()) return pg_down_;
    return down_;
}
std::string Migration::checksum() const {
    std::uint64_t h = UINT64_C(14695981039346656037);
    hash_string(h, "liteorm-migration-v1");
    hash_string(h, version_);
    hash_steps(h, up_);
    hash_steps(h, down_);
    hash_steps(h, sqlite_up_);
    hash_steps(h, sqlite_down_);
    hash_steps(h, pg_up_);
    hash_steps(h, pg_down_);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::hex << std::setfill('0') << std::setw(16) << h;
    return out.str();
}

void MigrationRunner::take_lock() {
    if (db_.dialect() == SqlDialect::PostgreSQL) {
        // One advisory xact lock per database, also protects creating history table.
        db_.execute_sql("SELECT pg_advisory_xact_lock(74129461020)");
    }
    // SQLite's Database::transaction() uses BEGIN IMMEDIATE; this obtains its writer lock.
}
void MigrationRunner::ensure_table() {
    db_.execute_sql(std::string("CREATE TABLE IF NOT EXISTS ") + history_table +
        " (\"version\" TEXT PRIMARY KEY, \"checksum\" TEXT NOT NULL, "
        "\"applied_at\" TEXT NOT NULL DEFAULT (CAST(CURRENT_TIMESTAMP AS TEXT)))");
}
std::vector<MigrationRecord> MigrationRunner::read_history() {
    SqlPlan plan(db_.dialect());
    plan.sql = std::string("SELECT \"version\", \"checksum\", \"applied_at\" FROM ") +
               history_table + " ORDER BY \"version\" ASC";
    Statement stmt(db_, plan);
    if (stmt.column_count() != 3) throw std::runtime_error("migration history has incompatible schema");
    std::vector<MigrationRecord> rows;
    while (stmt.step()) {
        MigrationRecord row;
        row.version = ValueCodec<std::string>::decode(stmt.column(0));
        row.checksum = ValueCodec<std::string>::decode(stmt.column(1));
        row.applied_at = ValueCodec<std::string>::decode(stmt.column(2));
        rows.push_back(row);
    }
    return rows;
}
void MigrationRunner::validate_registry(const std::vector<Migration>& migrations) const {
    for (std::size_t i = 0; i < migrations.size(); ++i) {
        if (i && migrations[i].version() <= migrations[i - 1].version())
            throw std::invalid_argument("migration versions must be strictly increasing (use zero-padded IDs)");
        if (migrations[i].up_steps(db_.dialect()).empty())
            throw std::invalid_argument("migration has no up SQL for the selected backend: " + migrations[i].version());
    }
}
void MigrationRunner::validate_history(const std::vector<Migration>& migrations,
                                       const std::vector<MigrationRecord>& records) const {
    if (records.size() > migrations.size())
        throw std::logic_error("database has migrations missing from supplied registry");
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (migrations[i].version() != records[i].version)
            throw std::logic_error("migration history is not a prefix of the supplied registry; missing/reordered: " + records[i].version);
        if (migrations[i].checksum() != records[i].checksum)
            throw std::logic_error("migration checksum mismatch (previously applied source changed): " + records[i].version);
    }
}
void MigrationRunner::run_steps(const std::vector<std::string>& sql) {
    for (std::size_t i = 0; i < sql.size(); ++i) db_.execute_sql(sql[i]);
}
void MigrationRunner::record(const Migration& migration) {
    SqlPlan plan(db_.dialect());
    plan.sql = std::string("INSERT INTO ") + history_table +
               " (\"version\", \"checksum\") VALUES (";
    append_parameter(plan, Value(migration.version()));
    plan.sql += ", ";
    append_parameter(plan, Value(migration.checksum()));
    plan.sql += ")";
    if (db_.execute(plan) != 1) throw std::runtime_error("migration history insert affected != 1 row");
}
void MigrationRunner::erase(const std::string& version) {
    SqlPlan plan(db_.dialect());
    plan.sql = std::string("DELETE FROM ") + history_table + " WHERE \"version\" = ";
    append_parameter(plan, Value(version));
    if (db_.execute(plan) != 1) throw std::runtime_error("migration history delete affected != 1 row");
}
MigrationReport MigrationRunner::apply_all(const std::vector<Migration>& migrations) {
    validate_registry(migrations);
    Transaction tx = db_.transaction();
    take_lock();
    ensure_table();
    const std::vector<MigrationRecord> history = read_history();
    validate_history(migrations, history);
    MigrationReport report;
    for (std::size_t i = 0; i < migrations.size(); ++i) {
        if (i < history.size()) { report.skipped.push_back(migrations[i].version()); continue; }
        run_steps(migrations[i].up_steps(db_.dialect()));
        record(migrations[i]);
        report.applied.push_back(migrations[i].version());
    }
    tx.commit();
    return report;
}
std::vector<std::string> MigrationRunner::pending(const std::vector<Migration>& migrations) {
    validate_registry(migrations);
    Transaction tx = db_.transaction();
    take_lock();
    ensure_table();
    const std::vector<MigrationRecord> history = read_history();
    validate_history(migrations, history);
    std::vector<std::string> ids;
    for (std::size_t i = history.size(); i < migrations.size(); ++i)
        ids.push_back(migrations[i].version());
    tx.commit();
    return ids;
}
std::vector<MigrationRecord> MigrationRunner::applied() {
    Transaction tx = db_.transaction();
    take_lock();
    ensure_table();
    const std::vector<MigrationRecord> rows = read_history();
    tx.commit();
    return rows;
}
bool MigrationRunner::rollback_last(const std::vector<Migration>& migrations) {
    validate_registry(migrations);
    Transaction tx = db_.transaction();
    take_lock();
    ensure_table();
    const std::vector<MigrationRecord> rows = read_history();
    validate_history(migrations, rows);
    if (rows.empty()) { tx.commit(); return false; }
    const Migration& migration = migrations[rows.size() - 1];
    const std::vector<std::string>& down = migration.down_steps(db_.dialect());
    if (down.empty())
        throw std::logic_error("migration is irreversible: " + migration.version());
    run_steps(down);
    erase(migration.version());
    tx.commit();
    return true;
}
} // namespace liteorm
