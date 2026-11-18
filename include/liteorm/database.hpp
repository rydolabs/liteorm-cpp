#ifndef LITEORM_DATABASE_HPP
#define LITEORM_DATABASE_HPP

#include "liteorm/core.hpp"
#include <map>
#include <memory>
#include <typeindex>
#include <stdexcept>
#include <string>
#include <vector>

namespace liteorm {

// The CMake build may omit libpq. SQLite is always available.
enum class BackendKind { SQLite, PostgreSQL };

class SqliteError : public std::runtime_error {
public:
    SqliteError(const std::string& message, int code)
        : std::runtime_error(message), code_(code) {}
    int code() const { return code_; }
private:
    int code_;
};

class PostgresError : public std::runtime_error {
public:
    PostgresError(const std::string& message, const std::string& state)
        : std::runtime_error(message), sqlstate_(state) {}
    const std::string& sqlstate() const { return sqlstate_; }
private:
    std::string sqlstate_;
};

namespace detail {
class Backend; class ResultCursor;
struct DatabaseLifetime { bool alive; DatabaseLifetime() : alive(true) {} };
struct Registration { bool ready; bool failed; Registration() : ready(false), failed(false) {} };
}
class Database;

// Cross-database result cursor; SQLite streams rows, libpq buffers a PGresult.
class Statement {
public:
    Statement(Database& db, const SqlPlan& plan);
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    bool step();
    int column_count() const;
    Value column(int index) const;
private:
    std::unique_ptr<detail::ResultCursor> cursor_;
    std::shared_ptr<detail::DatabaseLifetime> lifetime_;
};

class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction() noexcept;
    Transaction(Transaction&& other) noexcept;
    Transaction& operator=(Transaction&& other) = delete;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();
    void rollback();
private:
    Database* db_;
    bool active_;
    std::shared_ptr<detail::DatabaseLifetime> lifetime_;
};

template <typename Model> class ModelBuilder;
template <typename Model> class Repository;
template <typename Model> class RepositoryQuery;
template <typename Model> class Predicate;
template <typename Model> class BoundSelect;
template <typename Model> class BoundUpdate;
template <typename Model> class BoundDelete;
template <typename Left, typename Right> class BoundJoin;
template <typename First, typename... Parts> class BoundJoinedQuery;

// Compile model DDL without executing it, for use in explicit migrations.
// Identifiers come from trusted C++ metadata; do not pass unsanitized user input.
template <typename Model>
std::string create_table_sql(const Table<Model>& table, SqlDialect dialect,
                             bool if_not_exists = true) {
    if (table.size() == 0) throw std::logic_error("cannot create unmapped table");
    std::string sql = "CREATE TABLE ";
    if (if_not_exists) sql += "IF NOT EXISTS ";
    sql += quote_ident(table.name()) + " (";
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (i) sql += ", ";
        const ColumnInfo& info = table.info(i);
        if (dialect == SqlDialect::PostgreSQL) {
            if (info.generated) {
                if (info.pg_type == "BIGINT") sql += quote_ident(info.name) + " BIGSERIAL";
                else if (info.pg_type == "INTEGER") sql += quote_ident(info.name) + " SERIAL";
                else if (info.pg_type == "SMALLINT") sql += quote_ident(info.name) + " SMALLSERIAL";
                else throw std::logic_error("unsupported PostgreSQL generated type");
            } else sql += quote_ident(info.name) + " " + info.pg_type;
        } else {
            sql += quote_ident(info.name) + " " + info.sql_type;
        }
        if (info.primary_key) sql += " PRIMARY KEY";
        if (info.generated && dialect == SqlDialect::SQLite) sql += " AUTOINCREMENT";
        if (!info.nullable && !info.primary_key) sql += " NOT NULL";
        if (info.unique && !info.primary_key) sql += " UNIQUE";
        if (!info.foreign_table.empty())
            sql += " REFERENCES " + quote_ident(info.foreign_table) + "(" +
                   quote_ident(info.foreign_column) + ")";
    }
    sql += ")";
    return sql;
}

class Database {
public:
    // Keep v0.1's Database(":memory:") constructor working.
    explicit Database(const std::string& connection,
                      BackendKind backend = BackendKind::SQLite);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    // C++11 factories return an owned Database by value.
    // Move only before creating repositories, statements, or transactions.
    Database(Database&& other); // Rejects a move while dependent handles exist
    Database& operator=(Database&& other) = delete;

    // Preferred explicit backend selection; legacy constructors remain available.
    static Database sqlite(const std::string& filename);
    static Database postgresql(const std::string& conninfo);

    SqlDialect dialect() const;
    BackendKind backend_kind() const;
    // Explicitly trusted SQL ONLY (DDL / PRAGMA / SET); do not concatenate user values.
    void execute_sql(const std::string& sql);
    int execute(const SqlPlan& plan);
    Transaction transaction();
    std::int64_t last_insert_rowid() const; // SQLite only

    template <typename Model>
    void create_table(const Table<Model>& table, bool if_not_exists = true) {
        execute_sql(create_table_sql(table, dialect(), if_not_exists));
    }

    // Returns an integer primary key (generated or explicitly supplied),
    // or zero if the model has no integer primary key.
    template <typename Model>
    std::int64_t insert(const Table<Model>& table, const Model& value) {
        SqlPlan plan = insert_plan(table, value, dialect());
        const int index = table.primary_index();
        if (index < 0) { execute(plan); return 0; }
        const std::size_t pk = static_cast<std::size_t>(index);
        const Value supplied = table.extract(pk, value);
        if (supplied.kind != Value::Integer) { execute(plan); return 0; }
        if (dialect() == SqlDialect::PostgreSQL) {
            plan.sql += " RETURNING " + quote_ident(table.info(pk).name);
            Statement stmt(*this, plan);
            if (stmt.column_count() != 1 || !stmt.step())
                throw std::runtime_error("PostgreSQL INSERT RETURNING did not return a row");
            const std::int64_t id = ValueCodec<std::int64_t>::decode(stmt.column(0));
            if (stmt.step()) throw std::runtime_error("INSERT RETURNING returned multiple rows");
            return id;
        }
        execute(plan);
        return table.info(pk).generated ? last_insert_rowid() : supplied.integer;
    }

    template <typename Model>
    ModelBuilder<Model> model(const std::string& table_name);
    template <typename Model>
    Repository<Model> repository();

    template <typename Model>
    BoundSelect<Model> select(const Table<Model>& table);
    template <typename Model>
    BoundUpdate<Model> update(const Table<Model>& table);
    template <typename Model>
    BoundDelete<Model> remove(const Table<Model>& table);
    template <typename Left, typename Right>
    BoundJoinedQuery<Left, JoinedPart<Right, false> >
    join(const Table<Left>& left, const Table<Right>& right);
    template <typename Left, typename Right>
    BoundJoinedQuery<Left, JoinedPart<Right, true> >
    left_join(const Table<Left>& left, const Table<Right>& right);

    // A batch relation load performs one IN query rather than N individual queries.
    template <typename Parent, typename Child, typename Key>
    std::vector<std::vector<Child> > load_many(
        const HasMany<Parent,Child,Key>& relation,
        const std::vector<Parent>& parents);
    template <typename Child, typename Parent, typename Key>
    std::unique_ptr<Parent> load(
        const BelongsTo<Child,Parent,Key>& relation, const Child& child);
    template <typename Model, typename Key>
    std::unique_ptr<Model> find(const Table<Model>& table,
                                const Column<Model, Key>& key, const Key& id) {
        table.validate(key);
        if (table.primary_index() != static_cast<int>(key.index()))
            throw std::invalid_argument("find() expects registered primary key column");
        return select(table).where(key == id).first();
    }
private:
    friend class Statement;
    friend class Transaction;
    template <typename> friend class Repository;
    template <typename> friend class RepositoryQuery;
    std::unique_ptr<detail::Backend> driver_;
    std::map<std::type_index, std::shared_ptr<void> > models_;
    std::map<std::type_index, std::shared_ptr<detail::Registration> > model_status_;
    std::shared_ptr<detail::DatabaseLifetime> lifetime_;
};

template <typename Model>
class BoundSelect {
public:
    BoundSelect(Database& db, const Table<Model>& table)
        : db_(&db), table_(&table), query_(table) {}
    BoundSelect where(const Expr& e) const {
        BoundSelect q(*this); q.query_ = q.query_.where(e); return q;
    }
    BoundSelect order_by(const Order& o) const {
        BoundSelect q(*this); q.query_ = q.query_.order_by(o); return q;
    }
    BoundSelect limit(int n) const {
        BoundSelect q(*this); q.query_ = q.query_.limit(n); return q;
    }
    SqlPlan compile() const { return query_.compile(db_->dialect()); }
    // WHERE-only row count: intentionally ignores order_by() and limit().
    std::int64_t count() const {
        Statement stmt(*db_, query_.compile_count(db_->dialect()));
        if (stmt.column_count() != 1 || !stmt.step())
            throw std::runtime_error("COUNT query did not return one column and row");
        const std::int64_t n = ValueCodec<std::int64_t>::decode(stmt.column(0));
        if (stmt.step()) throw std::runtime_error("COUNT query returned more than one row");
        return n;
    }
    // WHERE-only existence check: ignores order_by() and limit().
    bool exists() const {
        Statement stmt(*db_, query_.compile_exists(db_->dialect()));
        if (stmt.column_count() != 1)
            throw std::runtime_error("EXISTS query did not return one column");
        return stmt.step();
    }
    std::vector<Model> all() const {
        Statement stmt(*db_, compile());
        if (stmt.column_count() != static_cast<int>(table_->size()))
            throw std::runtime_error("result column count does not match model");
        std::vector<Model> result;
        while (stmt.step()) {
            Model model = Model();
            for (std::size_t i = 0; i < table_->size(); ++i)
                table_->assign(i, model, stmt.column(static_cast<int>(i)));
            result.push_back(std::move(model));
        }
        return result;
    }
    std::unique_ptr<Model> first() const {
        Statement stmt(*db_, query_.limit(1).compile(db_->dialect()));
        if (stmt.column_count() != static_cast<int>(table_->size()))
            throw std::runtime_error("result column count does not match model");
        if (!stmt.step()) return std::unique_ptr<Model>();
        std::unique_ptr<Model> model(new Model());
        for (std::size_t i = 0; i < table_->size(); ++i)
            table_->assign(i, *model, stmt.column(static_cast<int>(i)));
        return model;
    }
private:
    Database* db_;
    const Table<Model>* table_;
    Query<Model> query_;
};

template <typename Model>
class BoundUpdate {
public:
    BoundUpdate(Database& db, const Table<Model>& table)
        : db_(&db), query_(table) {}
    template <typename Field>
    BoundUpdate set(const Column<Model, Field>& c, const Field& v) const {
        BoundUpdate q(*this); q.query_ = q.query_.set(c, v); return q;
    }
    BoundUpdate where(const Expr& e) const {
        BoundUpdate q(*this); q.query_ = q.query_.where(e); return q;
    }
    BoundUpdate all_rows() const {
        BoundUpdate q(*this); q.query_ = q.query_.all_rows(); return q;
    }
    SqlPlan compile() const { return query_.compile(db_->dialect()); }
    int execute() const { return db_->execute(compile()); }
private:
    Database* db_;
    UpdateQuery<Model> query_;
};

template <typename Model>
class BoundDelete {
public:
    BoundDelete(Database& db, const Table<Model>& table)
        : db_(&db), query_(table) {}
    BoundDelete where(const Expr& e) const {
        BoundDelete q(*this); q.query_ = q.query_.where(e); return q;
    }
    BoundDelete all_rows() const {
        BoundDelete q(*this); q.query_ = q.query_.all_rows(); return q;
    }
    SqlPlan compile() const { return query_.compile(db_->dialect()); }
    int execute() const { return db_->execute(compile()); }
private:
    Database* db_;
    DeleteQuery<Model> query_;
};

template <typename Left, typename Right>
class BoundJoin {
public:
    BoundJoin(Database& db, const Table<Left>& left, const Table<Right>& right)
        : db_(&db), left_(&left), right_(&right), query_(left, right) {}
    BoundJoin on(const Expr& e) const {
        BoundJoin q(*this); q.query_ = q.query_.on(e); return q;
    }
    BoundJoin where(const Expr& e) const {
        BoundJoin q(*this); q.query_ = q.query_.where(e); return q;
    }
    BoundJoin order_by(const Order& o) const {
        BoundJoin q(*this); q.query_ = q.query_.order_by(o); return q;
    }
    BoundJoin limit(int n) const {
        BoundJoin q(*this); q.query_ = q.query_.limit(n); return q;
    }
    SqlPlan compile() const { return query_.compile(db_->dialect()); }
    std::vector<std::pair<Left, Right> > all() const {
        Statement stmt(*db_, compile());
        if (stmt.column_count() != static_cast<int>(left_->size() + right_->size()))
            throw std::runtime_error("JOIN column count does not match models");
        std::vector<std::pair<Left, Right> > rows;
        while (stmt.step()) {
            Left l = Left();
            Right r = Right();
            for (std::size_t i = 0; i < left_->size(); ++i)
                left_->assign(i, l, stmt.column(static_cast<int>(i)));
            for (std::size_t i = 0; i < right_->size(); ++i)
                right_->assign(i, r, stmt.column(static_cast<int>(left_->size() + i)));
            rows.push_back(std::make_pair(std::move(l), std::move(r)));
        }
        return rows;
    }
    std::unique_ptr<std::pair<Left, Right> > first() const {
        Statement stmt(*db_, query_.limit(1).compile(db_->dialect()));
        if (stmt.column_count() != static_cast<int>(left_->size() + right_->size()))
            throw std::runtime_error("JOIN column count does not match models");
        if (!stmt.step()) return std::unique_ptr<std::pair<Left, Right> >();
        std::unique_ptr<std::pair<Left, Right> > row(new std::pair<Left, Right>());
        for (std::size_t i = 0; i < left_->size(); ++i)
            left_->assign(i, row->first, stmt.column(static_cast<int>(i)));
        for (std::size_t i = 0; i < right_->size(); ++i)
            right_->assign(i, row->second, stmt.column(static_cast<int>(left_->size() + i)));
        return row;
    }
private:
    Database* db_;
    const Table<Left>* left_;
    const Table<Right>* right_;
    JoinQuery<Left, Right> query_;
};


// Join result: pair for two tables (v0.2-compatible); tuple for 3+ tables.
template <typename First, typename... Parts>
struct JoinResult {
    typedef std::tuple<First, typename Parts::result_type...> tuple_type;
    typedef tuple_type type;
    static type finish(tuple_type&& row) { return std::move(row); }
};
template <typename First, typename Part>
struct JoinResult<First,Part> {
    typedef std::tuple<First,typename Part::result_type> tuple_type;
    typedef std::pair<First,typename Part::result_type> type;
    static type finish(tuple_type&& row) {
        return std::make_pair(std::move(std::get<0>(row)),
                              std::move(std::get<1>(row)));
    }
};

namespace detail {
template <typename Part, bool Outer = Part::left_outer>
struct PartReader;

template <typename Part>
struct PartReader<Part, false> {
    typedef typename Part::model_type Model;
    static Model read(const Part& part, const Statement& stmt, std::size_t offset) {
        Model model = Model();
        for (std::size_t i = 0; i < part.table->size(); ++i)
            part.table->assign(i, model, stmt.column(static_cast<int>(offset + i)));
        return model;
    }
};
template <typename Part>
struct PartReader<Part, true> {
    typedef typename Part::model_type Model;
    static Optional<Model> read(const Part& part, const Statement& stmt, std::size_t offset) {
        const int primary = part.table->primary_index();
        if (primary < 0) throw std::logic_error("LEFT JOIN needs right table primary key");
        if (stmt.column(static_cast<int>(offset + static_cast<std::size_t>(primary))).kind == Value::Null)
            return Optional<Model>();
        Model model = Model();
        for (std::size_t i = 0; i < part.table->size(); ++i)
            part.table->assign(i, model, stmt.column(static_cast<int>(offset + i)));
        return Optional<Model>(model);
    }
};

template <std::size_t I, typename PartsTuple, typename RowTuple>
typename std::enable_if<I == std::tuple_size<PartsTuple>::value, void>::type
read_join_parts(const PartsTuple&, const Statement&, RowTuple&, std::size_t) {}

template <std::size_t I, typename PartsTuple, typename RowTuple>
typename std::enable_if<I < std::tuple_size<PartsTuple>::value, void>::type
read_join_parts(const PartsTuple& parts, const Statement& stmt, RowTuple& row,
                std::size_t offset) {
    typedef typename std::tuple_element<I,PartsTuple>::type Part;
    const Part& part = std::get<I>(parts);
    std::get<I+1>(row) = PartReader<Part>::read(part, stmt, offset);
    read_join_parts<I+1>(parts, stmt, row, offset + part.table->size());
}
} // namespace detail

template <typename First, typename... Parts>
class BoundJoinedQuery {
public:
    typedef JoinChain<First,Parts...> Chain;
    typedef JoinResult<First, Parts...> Output;
    typedef typename Output::type Row;
    BoundJoinedQuery(Database& db, const Chain& chain) : db_(&db), chain_(chain) {}
    BoundJoinedQuery on(const Expr& expr) const {
        return BoundJoinedQuery(*db_, chain_.on(expr));
    }
    BoundJoinedQuery where(const Expr& expr) const {
        return BoundJoinedQuery(*db_, chain_.where(expr));
    }
    BoundJoinedQuery order_by(const Order& order) const {
        return BoundJoinedQuery(*db_, chain_.order_by(order));
    }
    BoundJoinedQuery limit(int count) const {
        return BoundJoinedQuery(*db_, chain_.limit(count));
    }
    template <typename Next>
    BoundJoinedQuery<First, Parts..., JoinedPart<Next,false> >
    join(const Table<Next>& table) const {
        return BoundJoinedQuery<First, Parts..., JoinedPart<Next,false> >(
            *db_, chain_.join(table));
    }
    template <typename Next>
    BoundJoinedQuery<First, Parts..., JoinedPart<Next,true> >
    left_join(const Table<Next>& table) const {
        return BoundJoinedQuery<First, Parts..., JoinedPart<Next,true> >(
            *db_, chain_.left_join(table));
    }
    SqlPlan compile() const { return chain_.compile(db_->dialect()); }
    std::vector<Row> all() const {
        Statement stmt(*db_, compile());
        ensure_columns(stmt);
        std::vector<Row> rows;
        while (stmt.step()) rows.push_back(read_row(stmt));
        return rows;
    }
    std::unique_ptr<Row> first() const {
        Statement stmt(*db_, chain_.limit(1).compile(db_->dialect()));
        ensure_columns(stmt);
        if (!stmt.step()) return std::unique_ptr<Row>();
        return std::unique_ptr<Row>(new Row(read_row(stmt)));
    }
private:
    void ensure_columns(const Statement& stmt) const {
        std::size_t count = chain_.first_table().size();
        count_join_columns<0>(count);
        if (static_cast<std::size_t>(stmt.column_count()) != count)
            throw std::runtime_error("JOIN result column count does not match models");
    }
    template <std::size_t I>
    typename std::enable_if<I == sizeof...(Parts),void>::type
    count_join_columns(std::size_t&) const {}
    template <std::size_t I>
    typename std::enable_if<I < sizeof...(Parts),void>::type
    count_join_columns(std::size_t& count) const {
        count += std::get<I>(chain_.parts()).table->size();
        count_join_columns<I+1>(count);
    }
    Row read_row(const Statement& stmt) const {
        typename Output::tuple_type row;
        First& base = std::get<0>(row);
        for (std::size_t i = 0; i < chain_.first_table().size(); ++i)
            chain_.first_table().assign(i, base, stmt.column(static_cast<int>(i)));
        detail::read_join_parts<0>(chain_.parts(), stmt, row, chain_.first_table().size());
        return Output::finish(std::move(row));
    }
    Database* db_;
    Chain chain_;
};

template <typename Left, typename Right>
BoundJoinedQuery<Left, JoinedPart<Right, false> >
Database::join(const Table<Left>& l, const Table<Right>& r) {
    typedef JoinedPart<Right, false> Part;
    return BoundJoinedQuery<Left,Part>(*this,
        JoinChain<Left,Part>(l, std::make_tuple(Part(r))));
}

template <typename Left, typename Right>
BoundJoinedQuery<Left, JoinedPart<Right, true> >
Database::left_join(const Table<Left>& l, const Table<Right>& r) {
    typedef JoinedPart<Right, true> Part;
    return BoundJoinedQuery<Left,Part>(*this,
        JoinChain<Left,Part>(l, std::make_tuple(Part(r))));
}

template <typename Parent, typename Child, typename Key>
std::vector<std::vector<Child> > Database::load_many(
    const HasMany<Parent,Child,Key>& rel, const std::vector<Parent>& parents) {
    std::vector<std::vector<Child> > grouped(parents.size());
    if (parents.empty()) return grouped;
    // Deduplicate keys and map results back to all parent positions. std::map
    // is intentional: supported SQL key types are orderable C++ value types.
    std::map<Key,std::vector<std::size_t> > positions;
    for (std::size_t i = 0; i < parents.size(); ++i)
        positions[parents[i].*(rel.parent_key().member())].push_back(i);
    std::vector<Key> keys;
    keys.reserve(positions.size());
    for (typename std::map<Key,std::vector<std::size_t> >::const_iterator it =
            positions.begin(); it != positions.end(); ++it)
        keys.push_back(it->first);
    // Conservative cap compatible with SQLite versions using 999 bind slots.
    const std::size_t batch_size = 500;
    for (std::size_t begin = 0; begin < keys.size(); begin += batch_size) {
        const std::size_t end = std::min(keys.size(), begin + batch_size);
        const std::vector<Key> batch(keys.begin() + static_cast<std::ptrdiff_t>(begin),
                                     keys.begin() + static_cast<std::ptrdiff_t>(end));
        const std::vector<Child> children = select(rel.child())
            .where(rel.child_key().in(batch)).all();
        for (std::size_t c = 0; c < children.size(); ++c) {
            const Key& key = children[c].*(rel.child_key().member());
            const typename std::map<Key,std::vector<std::size_t> >::const_iterator it =
                positions.find(key);
            if (it == positions.end()) continue;
            for (std::size_t p = 0; p < it->second.size(); ++p)
                grouped[it->second[p]].push_back(children[c]);
        }
    }
    return grouped;
}

template <typename Child, typename Parent, typename Key>
std::unique_ptr<Parent> Database::load(
    const BelongsTo<Child,Parent,Key>& rel, const Child& child) {
    return find(rel.parent(), rel.primary_key(),
                child.*(rel.foreign_key().member()));
}

template <typename Model>
BoundSelect<Model> Database::select(const Table<Model>& t) { return BoundSelect<Model>(*this, t); }
template <typename Model>
BoundUpdate<Model> Database::update(const Table<Model>& t) { return BoundUpdate<Model>(*this, t); }
template <typename Model>
BoundDelete<Model> Database::remove(const Table<Model>& t) { return BoundDelete<Model>(*this, t); }

} // namespace liteorm

#include "liteorm/repository.hpp"
#endif
