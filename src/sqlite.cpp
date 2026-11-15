#include "backend.hpp"
#include "liteorm/database.hpp"
#include <sqlite3.h>
#include <limits>
#include <stdexcept>

namespace liteorm { namespace detail {
namespace {
void check(int rc, sqlite3* db, const char* action) {
    if (rc == SQLITE_OK) return;
    throw SqliteError(std::string(action) + ": " +
        (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc)), rc);
}
void bind_one(sqlite3_stmt* stmt, sqlite3* db, int index, const Value& v) {
    int rc = SQLITE_OK;
    switch (v.kind) {
    case Value::Null: rc = sqlite3_bind_null(stmt, index); break;
    case Value::Integer: rc = sqlite3_bind_int64(stmt, index, v.integer); break;
    case Value::Real: rc = sqlite3_bind_double(stmt, index, v.real); break;
    case Value::Text:
        if (v.text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::out_of_range("SQLite text parameter exceeds INT_MAX bytes");
        rc = sqlite3_bind_text(stmt, index, v.text.data(),
                              static_cast<int>(v.text.size()), SQLITE_TRANSIENT);
        break;
    }
    check(rc, db, "bind");
}

class SqliteCursor : public ResultCursor {
public:
    SqliteCursor(sqlite3* db, const SqlPlan& plan)
        : db_(db), stmt_(NULL), row_ready_(false), done_(false) {
        if (plan.sql.find('\0') != std::string::npos)
            throw std::invalid_argument("NUL in SQL statement");
        if (plan.params.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::out_of_range("too many SQL parameters");
        const char* tail = NULL;
        int rc = sqlite3_prepare_v2(db_, plan.sql.c_str(), -1, &stmt_, &tail);
        if (rc != SQLITE_OK) {
            const std::string message = sqlite3_errmsg(db_);
            if (stmt_) sqlite3_finalize(stmt_);
            stmt_ = NULL;
            throw SqliteError("prepare: " + message, rc);
        }
        if (!stmt_) throw std::invalid_argument("empty SQL plan");
        try {
            // No second statement or comments are accepted in parameterized plans.
            for (; tail && *tail; ++tail) {
                if (*tail != ' ' && *tail != '\t' && *tail != '\r' && *tail != '\n')
                    throw std::invalid_argument("SQL plan must contain exactly one statement");
            }
            if (sqlite3_bind_parameter_count(stmt_) != static_cast<int>(plan.params.size()))
                throw std::invalid_argument("SQL placeholder count does not match parameter count");
            for (std::size_t i = 0; i < plan.params.size(); ++i)
                bind_one(stmt_, db_, static_cast<int>(i + 1), plan.params[i]);
        } catch (...) {
            sqlite3_finalize(stmt_);
            stmt_ = NULL;
            throw;
        }
    }
    ~SqliteCursor() { if (stmt_) sqlite3_finalize(stmt_); }
    bool step() {
        if (done_) return false;
        int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) { row_ready_ = true; return true; }
        row_ready_ = false;
        if (rc == SQLITE_DONE) { done_ = true; return false; }
        check(rc, db_, "step");
        return false;
    }
    int column_count() const { return sqlite3_column_count(stmt_); }
    bool readonly() const { return sqlite3_stmt_readonly(stmt_) != 0; }
    Value column(int index) const {
        if (!row_ready_) throw std::logic_error("column() requires an active row");
        if (index < 0 || index >= sqlite3_column_count(stmt_))
            throw std::out_of_range("SQLite result column index out of range");
        switch (sqlite3_column_type(stmt_, index)) {
        case SQLITE_NULL: return Value::null();
        case SQLITE_INTEGER:
            return Value(static_cast<std::int64_t>(sqlite3_column_int64(stmt_, index)));
        case SQLITE_FLOAT: return Value(sqlite3_column_double(stmt_, index));
        case SQLITE_TEXT: {
            const unsigned char* p = sqlite3_column_text(stmt_, index);
            const int n = sqlite3_column_bytes(stmt_, index);
            if (!p && n > 0) throw std::runtime_error("SQLite text read returned null");
            return Value(std::string(p ? reinterpret_cast<const char*>(p) : "",
                                     static_cast<std::size_t>(n)));
        }
        case SQLITE_BLOB: throw std::runtime_error("BLOB fields are not supported");
        }
        throw std::runtime_error("unrecognized SQLite column type");
    }
private:
    sqlite3* db_;
    sqlite3_stmt* stmt_;
    bool row_ready_;
    bool done_;
};

class SqliteBackend : public Backend {
public:
    explicit SqliteBackend(const std::string& filename) : db_(NULL) {
        if (filename.find('\0') != std::string::npos)
            throw std::invalid_argument("NUL in SQLite filename");
        sqlite3* opened = NULL;
        const int rc = sqlite3_open_v2(filename.c_str(), &opened,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
        if (rc != SQLITE_OK) {
            const std::string error = opened ? sqlite3_errmsg(opened) : sqlite3_errstr(rc);
            if (opened) sqlite3_close(opened);
            throw SqliteError("open database: " + error, rc);
        }
        db_ = opened;
        try {
            check(sqlite3_busy_timeout(db_, 5000), db_, "busy_timeout");
            execute_sql("PRAGMA foreign_keys = ON");
        } catch (...) { sqlite3_close(db_); db_ = NULL; throw; }
    }
    ~SqliteBackend() { if (db_) sqlite3_close_v2(db_); }
    SqlDialect dialect() const { return SqlDialect::SQLite; }
    void execute_sql(const std::string& sql) {
        if (sql.find('\0') != std::string::npos)
            throw std::invalid_argument("NUL in SQL statement");
        char* error = NULL;
        const int rc = sqlite3_exec(db_, sql.c_str(), NULL, NULL, &error);
        if (rc != SQLITE_OK) {
            const std::string message = error ? error : sqlite3_errmsg(db_);
            sqlite3_free(error);
            throw SqliteError("execute_sql: " + message, rc);
        }
    }
    int execute(const SqlPlan& plan) {
        if (plan.dialect != SqlDialect::SQLite)
            throw std::invalid_argument("SQLite backend requires SQLite SQL plan");
        SqliteCursor cursor(db_, plan);
        // A read-only SELECT that returns zero rows is not a mutation. Without
        // this guard sqlite3_changes() can report the previous write's count.
        if (cursor.readonly())
            throw std::invalid_argument("execute() requires a mutating SQL statement");
        if (cursor.step()) throw std::logic_error("mutation unexpectedly returned rows");
        return sqlite3_changes(db_);
    }
    std::unique_ptr<ResultCursor> query(const SqlPlan& plan) {
        if (plan.dialect != SqlDialect::SQLite)
            throw std::invalid_argument("SQLite backend requires SQLite SQL plan");
        return std::unique_ptr<ResultCursor>(new SqliteCursor(db_, plan));
    }
    std::int64_t last_insert_rowid() const { return sqlite3_last_insert_rowid(db_); }
    void begin() {
        if (!sqlite3_get_autocommit(db_))
            throw std::logic_error("nested SQLite transactions are not supported");
        execute_sql("BEGIN IMMEDIATE");
    }
    void commit() { execute_sql("COMMIT"); }
    void rollback() { execute_sql("ROLLBACK"); }
private:
    sqlite3* db_;
};
} // anonymous namespace

std::unique_ptr<Backend> open_sqlite(const std::string& path) {
    return std::unique_ptr<Backend>(new SqliteBackend(path));
}
}} // namespace liteorm::detail
