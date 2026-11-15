#include "liteorm/database.hpp"
#include "backend.hpp"
#include <utility>

namespace liteorm {

Database::Database(const std::string& connection, BackendKind kind)
    : driver_(kind == BackendKind::SQLite
              ? detail::open_sqlite(connection)
              : detail::open_postgresql(connection)) {}
Database::~Database() {}
SqlDialect Database::dialect() const { return driver_->dialect(); }
BackendKind Database::backend_kind() const {
    return dialect() == SqlDialect::SQLite ? BackendKind::SQLite : BackendKind::PostgreSQL;
}
void Database::execute_sql(const std::string& sql) { driver_->execute_sql(sql); }
int Database::execute(const SqlPlan& plan) {
    if (plan.dialect != dialect())
        throw std::invalid_argument("SQL plan dialect does not match database backend");
    return driver_->execute(plan);
}
std::int64_t Database::last_insert_rowid() const { return driver_->last_insert_rowid(); }
Transaction Database::transaction() { return Transaction(*this); }

Statement::Statement(Database& db, const SqlPlan& plan) {
    if (plan.dialect != db.dialect())
        throw std::invalid_argument("SQL plan dialect does not match database backend");
    cursor_ = db.driver_->query(plan);
}
Statement::~Statement() {}
Statement::Statement(Statement&& other) noexcept : cursor_(std::move(other.cursor_)) {}
Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) cursor_ = std::move(other.cursor_);
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

Transaction::Transaction(Database& db) : db_(&db), active_(false) {
    db.driver_->begin();
    active_ = true;
}
Transaction::~Transaction() noexcept {
    if (active_ && db_) {
        try { db_->driver_->rollback(); } catch (...) {}
    }
}
Transaction::Transaction(Transaction&& other) noexcept
    : db_(other.db_), active_(other.active_) {
    other.db_ = NULL;
    other.active_ = false;
}
void Transaction::commit() {
    if (!active_ || !db_) throw std::logic_error("transaction already completed");
    db_->driver_->commit();
    active_ = false;
}
void Transaction::rollback() {
    if (!active_ || !db_) throw std::logic_error("transaction already completed");
    db_->driver_->rollback();
    active_ = false;
}
} // namespace liteorm
