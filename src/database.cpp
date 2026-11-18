#include "liteorm/database.hpp"
#include "backend.hpp"
#include <utility>

namespace liteorm {

Database::Database(const std::string& connection, BackendKind kind)
    : driver_(kind == BackendKind::SQLite
              ? detail::open_sqlite(connection)
              : detail::open_postgresql(connection)),
      lifetime_(new detail::DatabaseLifetime()) {}
Database::~Database() { if (lifetime_) lifetime_->alive = false; }
Database::Database(Database&& other) : driver_() {
    if (!other.lifetime_ || !other.driver_)
        throw std::logic_error("moving a moved-from Database");
    if (other.lifetime_.use_count() != 1)
        throw std::logic_error("cannot move Database while dependent handles exist");
    driver_ = std::move(other.driver_);
    models_ = std::move(other.models_);
    model_status_ = std::move(other.model_status_);
    lifetime_ = std::move(other.lifetime_);
}
Database Database::sqlite(const std::string& filename) {
    return Database(filename, BackendKind::SQLite);
}
Database Database::postgresql(const std::string& conninfo) {
    return Database(conninfo, BackendKind::PostgreSQL);
}
SqlDialect Database::dialect() const {
    if (!driver_) throw std::logic_error("using moved-from Database");
    return driver_->dialect();
}
BackendKind Database::backend_kind() const {
    return dialect() == SqlDialect::SQLite ? BackendKind::SQLite : BackendKind::PostgreSQL;
}
void Database::execute_sql(const std::string& sql) {
    if (!driver_) throw std::logic_error("using moved-from Database");
    driver_->execute_sql(sql);
}
int Database::execute(const SqlPlan& plan) {
    if (plan.dialect != dialect())
        throw std::invalid_argument("SQL plan dialect does not match database backend");
    return driver_->execute(plan);
}
std::int64_t Database::last_insert_rowid() const {
    if (!driver_) throw std::logic_error("using moved-from Database");
    return driver_->last_insert_rowid();
}
Transaction Database::transaction() { return Transaction(*this); }

Statement::Statement(Database& db, const SqlPlan& plan) : lifetime_(db.lifetime_) {
    if (plan.dialect != db.dialect())
        throw std::invalid_argument("SQL plan dialect does not match database backend");
    cursor_ = db.driver_->query(plan);
}
Statement::~Statement() {}
Statement::Statement(Statement&& other) noexcept
    : cursor_(std::move(other.cursor_)), lifetime_(std::move(other.lifetime_)) {}
Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        cursor_ = std::move(other.cursor_);
        lifetime_ = std::move(other.lifetime_);
    }
    return *this;
}
bool Statement::step() {
    if (!cursor_) throw std::logic_error("using moved-from Statement");
    return cursor_->step();
}
int Statement::column_count() const {
    if (!cursor_) throw std::logic_error("using moved-from Statement");
    return cursor_->column_count();
}
Value Statement::column(int index) const {
    if (!cursor_) throw std::logic_error("using moved-from Statement");
    return cursor_->column(index);
}

Transaction::Transaction(Database& db)
    : db_(&db), active_(false), lifetime_(db.lifetime_) {
    if (!db.driver_) throw std::logic_error("using moved-from Database");
    db.driver_->begin();
    active_ = true;
}
Transaction::~Transaction() noexcept {
    if (active_ && db_ && lifetime_ && lifetime_->alive) {
        try { db_->driver_->rollback(); } catch (...) {}
    }
}
Transaction::Transaction(Transaction&& other) noexcept
    : db_(other.db_), active_(other.active_), lifetime_(std::move(other.lifetime_)) {
    other.db_ = NULL;
    other.active_ = false;
}
void Transaction::commit() {
    if (!active_ || !db_) throw std::logic_error("transaction already completed");
    if (!lifetime_ || !lifetime_->alive) throw std::logic_error("Database is destroyed");
    db_->driver_->commit();
    active_ = false;
}
void Transaction::rollback() {
    if (!active_ || !db_) throw std::logic_error("transaction already completed");
    if (!lifetime_ || !lifetime_->alive) throw std::logic_error("Database is destroyed");
    db_->driver_->rollback();
    active_ = false;
}
} // namespace liteorm
