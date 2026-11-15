#include "backend.hpp"
#include "liteorm/database.hpp"
#include <libpq-fe.h>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace liteorm { namespace detail {
namespace {
std::string error_state(const PGresult* result) {
    const char* state = result ? PQresultErrorField(result, PG_DIAG_SQLSTATE) : NULL;
    return state ? std::string(state) : std::string();
}
void require_status(PGconn* db, PGresult* result, ExecStatusType expected,
                    const char* operation) {
    if (result && PQresultStatus(result) == expected) return;
    const std::string state = error_state(result);
    const char* message = result ? PQresultErrorMessage(result) : PQerrorMessage(db);
    throw PostgresError(std::string(operation) + ": " + (message ? message : "unknown error"), state);
}
std::int64_t read_integer(const char* text, const char* what) {
    if (!text || !*text) throw std::runtime_error(std::string("empty ") + what);
    errno = 0;
    char* end = NULL;
    const long long value = std::strtoll(text, &end, 10);
    if (errno == ERANGE || !end || *end != '\0')
        throw std::out_of_range(std::string("invalid or overflowing ") + what);
    return static_cast<std::int64_t>(value);
}
double read_real(const char* text) {
    if (!text || !*text) throw std::runtime_error("empty PostgreSQL REAL value");
    // PostgreSQL float output always uses '.'; strtod depends on the process locale.
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double result = 0;
    stream >> result;
    if (!stream || stream.peek() != std::char_traits<char>::eof() || !std::isfinite(result))
        throw std::runtime_error("invalid PostgreSQL floating-point result");
    return result;
}
std::string write_real(double v) {
    if (!std::isfinite(v)) throw std::invalid_argument("non-finite PostgreSQL parameter");
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << v;
    return stream.str();
}

class PGResultCursor : public ResultCursor {
public:
    explicit PGResultCursor(PGresult* result) : result_(result), row_(-1) {}
    ~PGResultCursor() { if (result_) PQclear(result_); }
    bool step() {
        if (row_ < PQntuples(result_)) ++row_;
        return row_ < PQntuples(result_);
    }
    int column_count() const { return PQnfields(result_); }
    Value column(int index) const {
        if (row_ < 0 || row_ >= PQntuples(result_))
            throw std::logic_error("column() requires an active row");
        if (index < 0 || index >= PQnfields(result_))
            throw std::out_of_range("PostgreSQL result column index out of range");
        if (PQgetisnull(result_, row_, index)) return Value::null();
        const char* text = PQgetvalue(result_, row_, index);
        const int bytes = PQgetlength(result_, row_, index);
        switch (PQftype(result_, index)) {
        case 16: // bool ('t' / 'f')
            if (bytes != 1 || (text[0] != 't' && text[0] != 'f'))
                throw std::runtime_error("invalid PostgreSQL boolean value");
            return Value(static_cast<std::int64_t>(text[0] == 't' ? 1 : 0));
        case 20: // int8
        case 21: // int2
        case 23: // int4
            return Value(read_integer(text, "PostgreSQL integer"));
        case 700: // float4
        case 701: // float8
            return Value(read_real(text));
        case 25: // text
        case 1042: // bpchar
        case 1043: // varchar
            return Value(std::string(text, static_cast<std::size_t>(bytes)));
        default:
            throw std::runtime_error("unsupported PostgreSQL result column OID " +
                                     std::to_string(PQftype(result_, index)));
        }
    }
private:
    PGresult* result_;
    int row_;
};

class PostgresqlBackend : public Backend {
public:
    explicit PostgresqlBackend(const std::string& conninfo) : conn_(NULL) {
        if (conninfo.find('\0') != std::string::npos)
            throw std::invalid_argument("NUL in PostgreSQL connection string");
        conn_ = PQconnectdb(conninfo.c_str());
        if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
            const std::string msg = conn_ ? PQerrorMessage(conn_) : "out of memory";
            if (conn_) PQfinish(conn_);
            conn_ = NULL;
            throw PostgresError("PostgreSQL connect: " + msg, "");
        }
        if (PQsetClientEncoding(conn_, "UTF8") != 0) {
            const std::string msg = PQerrorMessage(conn_);
            PQfinish(conn_); conn_ = NULL;
            throw PostgresError("PostgreSQL set UTF8: " + msg, "");
        }
    }
    ~PostgresqlBackend() { if (conn_) PQfinish(conn_); }
    SqlDialect dialect() const { return SqlDialect::PostgreSQL; }
    void execute_sql(const std::string& sql) {
        if (sql.find('\0') != std::string::npos)
            throw std::invalid_argument("NUL in SQL statement");
        PGresult* result = PQexec(conn_, sql.c_str());
        try {
            if (!result || (PQresultStatus(result) != PGRES_COMMAND_OK &&
                            PQresultStatus(result) != PGRES_TUPLES_OK))
                require_status(conn_, result, PGRES_COMMAND_OK, "PostgreSQL execute_sql");
        } catch (...) { if (result) PQclear(result); throw; }
        PQclear(result);
    }
    int execute(const SqlPlan& plan) {
        PGresult* result = exec_params(plan);
        try {
            require_status(conn_, result, PGRES_COMMAND_OK, "PostgreSQL execute");
            const char* affected = PQcmdTuples(result);
            const std::int64_t count = (affected && *affected)
                ? read_integer(affected, "PostgreSQL affected row count") : 0;
            if (count < 0 || count > std::numeric_limits<int>::max())
                throw std::out_of_range("affected row count does not fit int");
            PQclear(result);
            return static_cast<int>(count);
        } catch (...) { if (result) PQclear(result); throw; }
    }
    std::unique_ptr<ResultCursor> query(const SqlPlan& plan) {
        PGresult* result = exec_params(plan);
        try { require_status(conn_, result, PGRES_TUPLES_OK, "PostgreSQL query"); }
        catch (...) { if (result) PQclear(result); throw; }
        return std::unique_ptr<ResultCursor>(new PGResultCursor(result));
    }
    std::int64_t last_insert_rowid() const {
        throw std::logic_error("PostgreSQL has no connection-local last_insert_rowid; use INSERT RETURNING");
    }
    void begin() {
        if (PQtransactionStatus(conn_) != PQTRANS_IDLE)
            throw std::logic_error("nested PostgreSQL transactions are not supported");
        execute_sql("BEGIN");
    }
    void commit() {
        // An aborted PostgreSQL transaction can return COMMAND_OK with tag
        // ROLLBACK for COMMIT. Do not report this as a successful commit.
        PGresult* result = PQexec(conn_, "COMMIT");
        try {
            require_status(conn_, result, PGRES_COMMAND_OK, "PostgreSQL commit");
            const char* tag = PQcmdStatus(result);
            if (!tag || std::string(tag) != "COMMIT")
                throw PostgresError("PostgreSQL transaction did not commit (rolled back)", "");
        } catch (...) { if (result) PQclear(result); throw; }
        PQclear(result);
    }
    void rollback() { execute_sql("ROLLBACK"); }
private:
    PGresult* exec_params(const SqlPlan& plan) {
        if (plan.dialect != SqlDialect::PostgreSQL)
            throw std::invalid_argument("PostgreSQL backend requires PostgreSQL SQL plan");
        if (plan.sql.find('\0') != std::string::npos)
            throw std::invalid_argument("NUL in SQL statement");
        if (plan.params.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::out_of_range("too many PostgreSQL parameters");
        std::vector<std::string> storage(plan.params.size());
        std::vector<const char*> ptrs(plan.params.size(), NULL);
        for (std::size_t i = 0; i < plan.params.size(); ++i) {
            const Value& v = plan.params[i];
            switch (v.kind) {
            case Value::Null: continue;
            case Value::Integer: storage[i] = std::to_string(v.integer); break;
            case Value::Real: storage[i] = write_real(v.real); break;
            case Value::Text:
                if (v.text.find('\0') != std::string::npos)
                    throw std::invalid_argument("PostgreSQL TEXT does not support embedded NUL bytes");
                storage[i] = v.text;
                break;
            }
            ptrs[i] = storage[i].c_str();
        }
        return PQexecParams(conn_, plan.sql.c_str(), static_cast<int>(plan.params.size()),
                            NULL, ptrs.empty() ? NULL : &ptrs[0], NULL, NULL, 0);
    }
    PGconn* conn_;
};
} // anonymous namespace
std::unique_ptr<Backend> open_postgresql(const std::string& conninfo) {
    return std::unique_ptr<Backend>(new PostgresqlBackend(conninfo));
}
}} // namespace liteorm::detail
