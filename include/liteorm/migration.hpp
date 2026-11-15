#ifndef LITEORM_MIGRATION_HPP
#define LITEORM_MIGRATION_HPP

#include "liteorm/database.hpp"
#include <string>
#include <vector>

namespace liteorm {

// Explicit, source-controlled SQL migrations. A database-specific list
// REPLACES (not appends to) the shared list for that backend.
// Execute each entry as trusted SQL; never build entries from user input.
class Migration {
public:
    explicit Migration(const std::string& version);
    const std::string& version() const { return version_; }

    Migration& up(const std::string& sql);
    Migration& down(const std::string& sql);
    Migration& sqlite_up(const std::string& sql);
    Migration& sqlite_down(const std::string& sql);
    Migration& postgresql_up(const std::string& sql);
    Migration& postgresql_down(const std::string& sql);

    const std::vector<std::string>& up_steps(SqlDialect dialect) const;
    const std::vector<std::string>& down_steps(SqlDialect dialect) const;
    // Stable FNV-1a fingerprint detects accidental edits, NOT tampering.
    std::string checksum() const;
private:
    std::string version_;
    std::vector<std::string> up_, down_, sqlite_up_, sqlite_down_, pg_up_, pg_down_;
};

struct MigrationRecord {
    std::string version;
    std::string checksum;
    std::string applied_at; // UTC-ish DB CURRENT_TIMESTAMP; DB/server timezone controls rendering
};

struct MigrationReport {
    std::vector<std::string> applied;
    std::vector<std::string> skipped;
};

class MigrationRunner {
public:
    explicit MigrationRunner(Database& db) : db_(db) {}

    // Lock, validate history and atomically apply all pending migrations.
    // Throws if applied migrations are missing, reordered, or modified.
    MigrationReport apply_all(const std::vector<Migration>& migrations);

    // Return pending versions after validating the complete supplied registry.
    // Creates the internal metadata table if missing.
    std::vector<std::string> pending(const std::vector<Migration>& migrations);

    // Read applied history in ascending order. Creates metadata table if missing.
    std::vector<MigrationRecord> applied();

    // Run the last applied migration's down steps in declaration order.
    // Returns false if history is empty; requires explicit down SQL otherwise.
    bool rollback_last(const std::vector<Migration>& migrations);
private:
    Database& db_;
    void take_lock();
    void ensure_table();
    std::vector<MigrationRecord> read_history();
    void validate_registry(const std::vector<Migration>& migrations) const;
    void validate_history(const std::vector<Migration>& migrations,
                          const std::vector<MigrationRecord>& records) const;
    void run_steps(const std::vector<std::string>& sql);
    void record(const Migration& migration);
    void erase(const std::string& version);
};

} // namespace liteorm
#endif
