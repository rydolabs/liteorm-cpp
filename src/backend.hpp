#ifndef LITEORM_INTERNAL_BACKEND_HPP
#define LITEORM_INTERNAL_BACKEND_HPP
#include "liteorm/core.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace liteorm { namespace detail {
class ResultCursor {
public:
    virtual ~ResultCursor() {}
    virtual bool step() = 0;
    virtual int column_count() const = 0;
    virtual Value column(int index) const = 0;
};

class Backend {
public:
    virtual ~Backend() {}
    virtual SqlDialect dialect() const = 0;
    virtual void execute_sql(const std::string& sql) = 0;
    virtual int execute(const SqlPlan& plan) = 0;
    virtual std::unique_ptr<ResultCursor> query(const SqlPlan& plan) = 0;
    virtual std::int64_t last_insert_rowid() const = 0;
    virtual void begin() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;
};

std::unique_ptr<Backend> open_sqlite(const std::string& filename);
std::unique_ptr<Backend> open_postgresql(const std::string& conninfo);
}} // namespace liteorm::detail
#endif
