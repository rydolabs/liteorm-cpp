#ifndef LITEORM_CORE_HPP
#define LITEORM_CORE_HPP

// Portable C++11 ORM core. No SQLite dependency in this header.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <tuple>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace liteorm {

// Intentionally small C++11 substitute for std::optional, for value fields.
template <typename T>
class Optional {
public:
    Optional() : present_(false), value_() {}
    Optional(const T& value) : present_(true), value_(value) {}
    bool has_value() const { return present_; }
    const T& value() const {
        if (!present_) throw std::logic_error("Optional has no value");
        return value_;
    }
    T& value() {
        if (!present_) throw std::logic_error("Optional has no value");
        return value_;
    }
    void reset() { present_ = false; value_ = T(); }
    Optional& operator=(const T& v) { value_ = v; present_ = true; return *this; }
private:
    bool present_;
    T value_;
};

struct Value {
    enum Kind { Null, Integer, Real, Text };
    Kind kind;
    std::int64_t integer;
    double real;
    std::string text;

    Value() : kind(Null), integer(0), real(0) {}
    explicit Value(std::int64_t v) : kind(Integer), integer(v), real(0) {}
    explicit Value(double v) : kind(Real), integer(0), real(v) {}
    explicit Value(const std::string& v) : kind(Text), integer(0), real(0), text(v) {}
    static Value null() { return Value(); }
};

// Only explicitly supported field types compile. Extend with ValueCodec<T>.
template <typename T, typename Enable = void>
struct ValueCodec;

template <typename T>
struct ValueCodec<T, typename std::enable_if<
    std::is_integral<T>::value && !std::is_same<T, bool>::value>::type> {
    static Value encode(T v) {
        if (!std::numeric_limits<T>::is_signed && sizeof(T) >= sizeof(std::int64_t) &&
            static_cast<std::uint64_t>(v) >
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            throw std::out_of_range("unsigned integer cannot fit into signed SQL BIGINT");
        return Value(static_cast<std::int64_t>(v));
    }
    static T decode(const Value& v) {
        if (v.kind != Value::Integer) throw std::runtime_error("expected INTEGER column");
        if (std::numeric_limits<T>::is_signed) {
            if (v.integer < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
                v.integer > static_cast<std::int64_t>(std::numeric_limits<T>::max()))
                throw std::out_of_range("integer column outside C++ field range");
        } else if (v.integer < 0 ||
                   static_cast<std::uint64_t>(v.integer) >
                       static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
            throw std::out_of_range("integer column outside unsigned C++ field range");
        }
        return static_cast<T>(v.integer);
    }
    static const char* sql_type() { return "INTEGER"; }
    // PostgreSQL has only signed integer columns. Choose a wider SQL type for
    // unsigned C++ fields so their full representable range fits when possible.
    // uint64_t is constrained to INT64_MAX by encode(), not silently wrapped.
    static const char* pg_type() {
        if (sizeof(T) <= 1) return "SMALLINT";
        if (sizeof(T) <= 2) return std::numeric_limits<T>::is_signed ? "SMALLINT" : "INTEGER";
        if (sizeof(T) <= 4) return std::numeric_limits<T>::is_signed ? "INTEGER" : "BIGINT";
        return "BIGINT";
    }
    static bool identity_compatible() { return true; }
    static bool nullable() { return false; }
};

template <> struct ValueCodec<bool, void> {
    static Value encode(bool v) { return Value(static_cast<std::int64_t>(v ? 1 : 0)); }
    static bool decode(const Value& v) {
        if (v.kind != Value::Integer || (v.integer != 0 && v.integer != 1))
            throw std::runtime_error("expected boolean (0 or 1)");
        return v.integer == 1;
    }
    static const char* sql_type() { return "INTEGER"; }
    static const char* pg_type() { return "BOOLEAN"; }
    static bool identity_compatible() { return false; }
    static bool nullable() { return false; }
};

template <typename T>
struct ValueCodec<T, typename std::enable_if<std::is_floating_point<T>::value>::type> {
    static Value encode(T v) {
        if (!std::isfinite(static_cast<double>(v)))
            throw std::invalid_argument("non-finite floating-point database value");
        return Value(static_cast<double>(v));
    }
    static T decode(const Value& v) {
        double number = 0;
        if (v.kind == Value::Real) number = v.real;
        else if (v.kind == Value::Integer) number = static_cast<double>(v.integer);
        else throw std::runtime_error("expected REAL column");
        if (!std::isfinite(number) ||
            number > static_cast<double>(std::numeric_limits<T>::max()) ||
            number < -static_cast<double>(std::numeric_limits<T>::max()))
            throw std::out_of_range("REAL column outside C++ field range");
        return static_cast<T>(number);
    }
    static const char* sql_type() { return "REAL"; }
    static const char* pg_type() { return sizeof(T) <= 4 ? "REAL" : "DOUBLE PRECISION"; }
    static bool identity_compatible() { return false; }
    static bool nullable() { return false; }
};

template <> struct ValueCodec<std::string, void> {
    // SQLite preserves embedded NUL bytes in TEXT; PostgreSQL rejects them
    // at the driver boundary. Do not silently change this existing behavior.
    static Value encode(const std::string& v) { return Value(v); }
    static std::string decode(const Value& v) {
        if (v.kind != Value::Text) throw std::runtime_error("expected TEXT column");
        return v.text;
    }
    static const char* sql_type() { return "TEXT"; }
    static const char* pg_type() { return "TEXT"; }
    static bool identity_compatible() { return false; }
    static bool nullable() { return false; }
};

template <typename T>
struct ValueCodec<Optional<T>, void> {
    static Value encode(const Optional<T>& v) {
        return v.has_value() ? ValueCodec<T>::encode(v.value()) : Value::null();
    }
    static Optional<T> decode(const Value& v) {
        return v.kind == Value::Null ? Optional<T>() : Optional<T>(ValueCodec<T>::decode(v));
    }
    static const char* sql_type() { return ValueCodec<T>::sql_type(); }
    static const char* pg_type() { return ValueCodec<T>::pg_type(); }
    static bool identity_compatible() { return false; }
    static bool nullable() { return true; }
};

inline std::string quote_ident(const std::string& name) {
    if (name.empty()) throw std::invalid_argument("empty SQL identifier");
    std::string out = "\"";
    for (std::string::const_iterator it = name.begin(); it != name.end(); ++it) {
        if (*it == '\0') throw std::invalid_argument("NUL in SQL identifier");
        if (*it == '"') out += "\"\"";
        else out += *it;
    }
    return out + "\"";
}

enum class SqlDialect { SQLite, PostgreSQL };

struct SqlPlan {
    explicit SqlPlan(SqlDialect d = SqlDialect::SQLite) : dialect(d) {}
    SqlDialect dialect;
    std::string sql;
    std::vector<Value> params;
};

inline void append_parameter(SqlPlan& plan, const Value& value) {
    // The dialect owns bind-marker syntax; all values are sent out-of-band.
    if (plan.dialect == SqlDialect::PostgreSQL)
        plan.sql += "$" + std::to_string(plan.params.size() + 1);
    else
        plan.sql += "?";
    plan.params.push_back(value);
}

class Expr {
    struct Node {
        enum Kind { Comparison, And, Or, Not, IsNull, IsNotNull, In } kind;
        const void* table_owner;
        const void* rhs_owner;
        std::string column, rhs_column;
        bool rhs_is_column;
        std::string op;
        Value value;
        std::vector<Value> values;
        std::shared_ptr<const Node> left, right;
        explicit Node(Kind k) : kind(k), table_owner(NULL), rhs_owner(NULL), rhs_is_column(false) {}
    };
    std::shared_ptr<const Node> root_;
    explicit Expr(const std::shared_ptr<const Node>& n) : root_(n) {}
    static Expr binary(Node::Kind kind, const Expr& l, const Expr& r) {
        std::shared_ptr<Node> n(new Node(kind));
        n->left = l.root_;
        n->right = r.root_;
        return Expr(n);
    }
    static void check_owner(const std::shared_ptr<const Node>& node, const void* owner) {
        if (!node) throw std::logic_error("empty predicate");
        if ((node->table_owner && node->table_owner != owner) ||
            (node->rhs_owner && node->rhs_owner != owner))
            throw std::invalid_argument("predicate belongs to another table");
        if (node->left) check_owner(node->left, owner);
        if (node->right) check_owner(node->right, owner);
    }
    static void check_join_scope(const std::shared_ptr<const Node>& node,
                                 const void* left, const void* right) {
        if (!node) throw std::logic_error("empty predicate");
        if ((node->table_owner && node->table_owner != left && node->table_owner != right) ||
            (node->rhs_owner && node->rhs_owner != left && node->rhs_owner != right))
            throw std::invalid_argument("predicate belongs to an unrelated table");
        if (node->left) check_join_scope(node->left, left, right);
        if (node->right) check_join_scope(node->right, left, right);
    }
    static void check_scope(const std::shared_ptr<const Node>& node,
                            const std::vector<const void*>& owners) {
        if (!node) throw std::logic_error("empty predicate");
        if (node->table_owner &&
            std::find(owners.begin(), owners.end(), node->table_owner) == owners.end())
            throw std::invalid_argument("predicate belongs to an unrelated table");
        if (node->rhs_owner &&
            std::find(owners.begin(), owners.end(), node->rhs_owner) == owners.end())
            throw std::invalid_argument("predicate belongs to an unrelated table");
        if (node->left) check_scope(node->left, owners);
        if (node->right) check_scope(node->right, owners);
    }
    static void emit(const std::shared_ptr<const Node>& n, SqlPlan& plan) {
        if (n->kind == Node::In) {
            if (n->values.empty()) { plan.sql += "(1 = 0)"; return; }
            plan.sql += n->column + " IN (";
            for (std::size_t i = 0; i < n->values.size(); ++i) {
                if (i) plan.sql += ", ";
                append_parameter(plan, n->values[i]);
            }
            plan.sql += ")";
        } else if (n->kind == Node::Comparison) {
            plan.sql += n->column + " " + n->op + " ";
            if (n->rhs_is_column) plan.sql += n->rhs_column;
            else append_parameter(plan, n->value);
        } else if (n->kind == Node::IsNull || n->kind == Node::IsNotNull) {
            plan.sql += n->column + (n->kind == Node::IsNull ? " IS NULL" : " IS NOT NULL");
        } else if (n->kind == Node::Not) {
            plan.sql += "(NOT ("; emit(n->left, plan); plan.sql += "))";
        } else {
            plan.sql += "(";
            emit(n->left, plan);
            plan.sql += (n->kind == Node::And ? " AND " : " OR ");
            emit(n->right, plan);
            plan.sql += ")";
        }
    }
public:
    Expr() {}
    static Expr compare(const void* owner, const std::string& qualified,
                        const char* op, const Value& v) {
        std::shared_ptr<Node> n(new Node(Node::Comparison));
        n->table_owner = owner;
        n->column = qualified;
        n->op = op;
        n->value = v;
        return Expr(n);
    }
    static Expr compare_columns(const void* left_owner, const std::string& left_column,
                                const char* op, const void* right_owner,
                                const std::string& right_column) {
        std::shared_ptr<Node> n(new Node(Node::Comparison));
        n->table_owner = left_owner;
        n->rhs_owner = right_owner;
        n->column = left_column;
        n->rhs_column = right_column;
        n->rhs_is_column = true;
        n->op = op;
        return Expr(n);
    }
    static Expr in_values(const void* owner, const std::string& qualified,
                          const std::vector<Value>& values) {
        std::shared_ptr<Node> n(new Node(Node::In));
        n->table_owner = owner;
        n->column = qualified;
        n->values = values;
        return Expr(n);
    }
    static Expr null_check(const void* owner, const std::string& qualified, bool is_null) {
        std::shared_ptr<Node> n(new Node(is_null ? Node::IsNull : Node::IsNotNull));
        n->table_owner = owner;
        n->column = qualified;
        return Expr(n);
    }
    void validate(const void* owner) const { check_owner(root_, owner); }
    void validate_scope(const std::vector<const void*>& owners) const {
        check_scope(root_, owners);
    }
    void validate_join(const void* left, const void* right) const {
        check_join_scope(root_, left, right);
    }
    void append_to(SqlPlan& plan) const {
        if (!root_) throw std::logic_error("empty predicate");
        emit(root_, plan);
    }
    friend Expr operator&&(const Expr& l, const Expr& r) { return binary(Node::And, l, r); }
    friend Expr operator||(const Expr& l, const Expr& r) { return binary(Node::Or, l, r); }
    friend Expr operator!(const Expr& e) {
        std::shared_ptr<Node> n(new Node(Node::Not));
        n->left = e.root_;
        return Expr(n);
    }
};

struct Order {
    const void* owner;
    std::string sql;
    Order(const void* o, const std::string& s) : owner(o), sql(s) {}
};

template <typename Model, typename Field>
class Column {
public:
    Column(const void* owner, const std::string& table,
           const std::string& name, Field Model::* member, std::size_t index)
        : owner_(owner), table_(table), name_(name), member_(member), index_(index) {}
    const void* owner() const { return owner_; }
    const std::string& name() const { return name_; }
    Field Model::* member() const { return member_; }
    std::size_t index() const { return index_; }
    std::string qualified_name() const { return qualified(); }
    template <typename OtherModel>
    Expr operator==(const Column<OtherModel, Field>& rhs) const {
        return Expr::compare_columns(owner_, qualified(), "=", rhs.owner(), rhs.qualified_name());
    }
    template <typename OtherModel>
    Expr operator!=(const Column<OtherModel, Field>& rhs) const {
        return Expr::compare_columns(owner_, qualified(), "<>", rhs.owner(), rhs.qualified_name());
    }
    Expr operator==(const Field& v) const { return compare("=", v); }
    Expr operator!=(const Field& v) const { return compare("<>", v); }
    Expr operator<(const Field& v) const { return compare("<", v); }
    Expr operator<=(const Field& v) const { return compare("<=", v); }
    Expr operator>(const Field& v) const { return compare(">", v); }
    Expr operator>=(const Field& v) const { return compare(">=", v); }
    Expr in(const std::vector<Field>& values) const {
        std::vector<Value> encoded;
        encoded.reserve(values.size());
        for (std::size_t i = 0; i < values.size(); ++i) {
            Value value = ValueCodec<Field>::encode(values[i]);
            if (value.kind == Value::Null)
                throw std::invalid_argument("IN list may not contain NULL; use is_null()");
            encoded.push_back(value);
        }
        return Expr::in_values(owner_, qualified(), encoded);
    }
    Expr is_null() const { return Expr::null_check(owner_, qualified(), true); }
    Expr is_not_null() const { return Expr::null_check(owner_, qualified(), false); }
    Order asc() const { return Order(owner_, qualified() + " ASC"); }
    Order desc() const { return Order(owner_, qualified() + " DESC"); }
private:
    const void* owner_;
    std::string table_, name_;
    Field Model::* member_;
    std::size_t index_;
    std::string qualified() const { return quote_ident(table_) + "." + quote_ident(name_); }
    Expr compare(const char* op, const Field& v) const {
        const Value encoded = ValueCodec<Field>::encode(v);
        if (encoded.kind == Value::Null)
            throw std::invalid_argument("compare against NULL with is_null()/is_not_null()");
        return Expr::compare(owner_, qualified(), op, encoded);
    }
};

struct ColumnOptions {
    bool primary_key;
    bool generated;
    bool unique;
    std::string foreign_table, foreign_column;
    ColumnOptions() : primary_key(false), generated(false), unique(false) {}
    static ColumnOptions identity() {
        ColumnOptions o; o.primary_key = true; o.generated = true; return o;
    }
    static ColumnOptions primary() {
        ColumnOptions o; o.primary_key = true; return o;
    }
    static ColumnOptions unique_key() {
        ColumnOptions o; o.unique = true; return o;
    }
    static ColumnOptions foreign_key(const std::string& table, const std::string& column) {
        quote_ident(table);
        quote_ident(column);
        ColumnOptions o;
        o.foreign_table = table; o.foreign_column = column;
        return o;
    }
};

struct ColumnInfo {
    std::string name, sql_type, pg_type, foreign_table, foreign_column;
    bool primary_key, generated, unique, nullable;
};

template <typename Model>
class FieldBinding {
public:
    explicit FieldBinding(const ColumnInfo& info) : info_(info) {}
    virtual ~FieldBinding() {}
    const ColumnInfo& info() const { return info_; }
    virtual Value extract(const Model&) const = 0;
    virtual void assign(Model&, const Value&) const = 0;
private:
    ColumnInfo info_;
};

template <typename Model, typename Field>
class MemberBinding : public FieldBinding<Model> {
public:
    MemberBinding(const ColumnInfo& info, Field Model::* member)
        : FieldBinding<Model>(info), member_(member) {}
    Value extract(const Model& m) const { return ValueCodec<Field>::encode(m.*member_); }
    void assign(Model& m, const Value& v) const { m.*member_ = ValueCodec<Field>::decode(v); }
private:
    Field Model::* member_;
};

template <typename Model>
class Table {
public:
    explicit Table(const std::string& name) : name_(name), primary_index_(-1) {
        quote_ident(name_); // validate as soon as registered
    }
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
    virtual ~Table() {}
    const std::string& name() const { return name_; }
    std::size_t size() const { return fields_.size(); }
    const ColumnInfo& info(std::size_t i) const { return fields_.at(i)->info(); }
    int primary_index() const { return primary_index_; }
    Value extract(std::size_t i, const Model& value) const { return fields_.at(i)->extract(value); }
    void assign(std::size_t i, Model& value, const Value& field) const {
        fields_.at(i)->assign(value, field);
    }
    template <typename Field>
    void validate(const Column<Model, Field>& column) const {
        if (column.owner() != this || column.index() >= fields_.size() ||
            fields_[column.index()]->info().name != column.name())
            throw std::invalid_argument("column does not belong to this table");
    }
protected:
    template <typename Field>
    Column<Model, Field> add(const std::string& name, Field Model::* member,
                             ColumnOptions opts = ColumnOptions()) {
        quote_ident(name);
        if (!member) throw std::invalid_argument("null model member pointer");
        for (std::size_t i = 0; i < fields_.size(); ++i)
            if (fields_[i]->info().name == name) throw std::invalid_argument("duplicate column");
        if (opts.primary_key && primary_index_ >= 0)
            throw std::invalid_argument("only one primary key supported in v0.1");
        if (opts.generated && (!opts.primary_key || !ValueCodec<Field>::identity_compatible()))
            throw std::invalid_argument("identity requires a non-boolean integer primary key");
        if (opts.primary_key && ValueCodec<Field>::nullable())
            throw std::invalid_argument("nullable primary key not supported");
        if (opts.generated && !opts.foreign_table.empty())
            throw std::invalid_argument("generated primary key cannot also be a foreign key");
        ColumnInfo info = {name, ValueCodec<Field>::sql_type(), ValueCodec<Field>::pg_type(),
                           opts.foreign_table, opts.foreign_column, opts.primary_key,
                           opts.generated, opts.unique, ValueCodec<Field>::nullable()};
        const std::size_t index = fields_.size();
        fields_.push_back(std::shared_ptr<FieldBinding<Model> >(
            new MemberBinding<Model, Field>(info, member)));
        if (opts.primary_key) primary_index_ = static_cast<int>(index);
        return Column<Model, Field>(this, name_, name, member, index);
    }
private:
    std::string name_;
    int primary_index_;
    std::vector<std::shared_ptr<FieldBinding<Model> > > fields_;
};

template <typename Model>
class Query {
public:
    explicit Query(const Table<Model>& table)
        : table_(&table), has_where_(false), limit_(-1) {}
    Query where(const Expr& expr) const {
        expr.validate(table_);
        Query q(*this);
        q.expr_ = q.has_where_ ? q.expr_ && expr : expr;
        q.has_where_ = true;
        return q;
    }
    Query order_by(const Order& o) const {
        if (o.owner != table_) throw std::invalid_argument("order column belongs to another table");
        Query q(*this);
        q.order_ = o.sql;
        return q;
    }
    Query limit(int n) const {
        if (n < 0) throw std::invalid_argument("negative LIMIT");
        Query q(*this); q.limit_ = n; return q;
    }
    SqlPlan compile(SqlDialect dialect = SqlDialect::SQLite) const {
        if (table_->size() == 0) throw std::logic_error("no registered model columns");
        SqlPlan plan(dialect);
        plan.sql = "SELECT ";
        for (std::size_t i = 0; i < table_->size(); ++i) {
            if (i) plan.sql += ", ";
            plan.sql += quote_ident(table_->name()) + "." + quote_ident(table_->info(i).name);
        }
        plan.sql += " FROM " + quote_ident(table_->name());
        if (has_where_) { plan.sql += " WHERE "; expr_.append_to(plan); }
        if (!order_.empty()) plan.sql += " ORDER BY " + order_;
        if (limit_ >= 0) {
            plan.sql += " LIMIT ";
            append_parameter(plan, ValueCodec<int>::encode(limit_));
        }
        return plan;
    }
    // Count matching rows, independent of ORDER BY and LIMIT.
    SqlPlan compile_count(SqlDialect dialect = SqlDialect::SQLite) const {
        if (table_->size() == 0) throw std::logic_error("no registered model columns");
        SqlPlan plan(dialect);
        plan.sql = "SELECT COUNT(*) FROM " + quote_ident(table_->name());
        if (has_where_) { plan.sql += " WHERE "; expr_.append_to(plan); }
        return plan;
    }
    // Existence checks read at most one row and do not materialize models.
    SqlPlan compile_exists(SqlDialect dialect = SqlDialect::SQLite) const {
        if (table_->size() == 0) throw std::logic_error("no registered model columns");
        SqlPlan plan(dialect);
        plan.sql = "SELECT 1 FROM " + quote_ident(table_->name());
        if (has_where_) { plan.sql += " WHERE "; expr_.append_to(plan); }
        plan.sql += " LIMIT 1";
        return plan;
    }
private:
    const Table<Model>* table_; // Table lifetime must exceed Query lifetime.
    bool has_where_;
    Expr expr_;
    std::string order_;
    int limit_;
};

template <typename Model>
inline Query<Model> select(const Table<Model>& table) { return Query<Model>(table); }

template <typename Model>
inline SqlPlan insert_plan(const Table<Model>& table, const Model& model,
                           SqlDialect dialect = SqlDialect::SQLite) {
    if (table.size() == 0)
        throw std::logic_error("INSERT requires at least one registered model column");
    SqlPlan plan(dialect);
    plan.sql = "INSERT INTO " + quote_ident(table.name());
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < table.size(); ++i)
        if (!table.info(i).generated) indices.push_back(i);
    if (indices.empty()) { plan.sql += " DEFAULT VALUES"; return plan; }
    plan.sql += " (";
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (i) plan.sql += ", ";
        plan.sql += quote_ident(table.info(indices[i]).name);
    }
    plan.sql += ") VALUES (";
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (i) plan.sql += ", ";
        append_parameter(plan, table.extract(indices[i], model));
    }
    plan.sql += ")";
    return plan;
}

template <typename Model>
class UpdateQuery {
public:
    explicit UpdateQuery(const Table<Model>& t) : table_(&t), has_where_(false), all_rows_(false) {}
    template <typename Field>
    UpdateQuery set(const Column<Model, Field>& c, const Field& value) const {
        table_->validate(c);
        if (table_->info(c.index()).generated)
            throw std::invalid_argument("cannot update generated primary key");
        UpdateQuery q(*this);
        for (std::size_t i = 0; i < q.assignments_.size(); ++i)
            if (q.assignments_[i].first == c.index())
                throw std::invalid_argument("duplicate SET column");
        q.assignments_.push_back(std::make_pair(c.index(), ValueCodec<Field>::encode(value)));
        return q;
    }
    UpdateQuery where(const Expr& e) const {
        e.validate(table_);
        UpdateQuery q(*this);
        q.expr_ = q.has_where_ ? q.expr_ && e : e;
        q.has_where_ = true;
        return q;
    }
    UpdateQuery all_rows() const { UpdateQuery q(*this); q.all_rows_ = true; return q; }
    SqlPlan compile(SqlDialect dialect = SqlDialect::SQLite) const {
        if (assignments_.empty()) throw std::logic_error("UPDATE needs SET");
        if (!has_where_ && !all_rows_)
            throw std::logic_error("UPDATE requires WHERE or explicit all_rows()");
        SqlPlan p(dialect);
        p.sql = "UPDATE " + quote_ident(table_->name()) + " SET ";
        for (std::size_t i = 0; i < assignments_.size(); ++i) {
            if (i) p.sql += ", ";
            p.sql += quote_ident(table_->info(assignments_[i].first).name) + " = ";
            append_parameter(p, assignments_[i].second);
        }
        if (has_where_) { p.sql += " WHERE "; expr_.append_to(p); }
        return p;
    }
private:
    const Table<Model>* table_;
    std::vector<std::pair<std::size_t, Value> > assignments_;
    bool has_where_, all_rows_;
    Expr expr_;
};

template <typename Model>
class DeleteQuery {
public:
    explicit DeleteQuery(const Table<Model>& t) : table_(&t), has_where_(false), all_rows_(false) {}
    DeleteQuery where(const Expr& e) const {
        e.validate(table_);
        DeleteQuery q(*this);
        q.expr_ = q.has_where_ ? q.expr_ && e : e;
        q.has_where_ = true;
        return q;
    }
    DeleteQuery all_rows() const { DeleteQuery q(*this); q.all_rows_ = true; return q; }
    SqlPlan compile(SqlDialect dialect = SqlDialect::SQLite) const {
        if (!has_where_ && !all_rows_)
            throw std::logic_error("DELETE requires WHERE or explicit all_rows()");
        SqlPlan p(dialect);
        p.sql = "DELETE FROM " + quote_ident(table_->name());
        if (has_where_) { p.sql += " WHERE "; expr_.append_to(p); }
        return p;
    }
private:
    const Table<Model>* table_;
    bool has_where_, all_rows_;
    Expr expr_;
};

// Typed two-table INNER JOIN. Both models are materialized by position.
// Self-joins and repeated SQL table names require aliases (not supported yet).
template <typename Left, typename Right>
class JoinQuery {
public:
    JoinQuery(const Table<Left>& left, const Table<Right>& right)
        : left_(&left), right_(&right), has_on_(false), has_where_(false), limit_(-1) {
        if (left.name() == right.name())
            throw std::invalid_argument("JOIN requires distinct SQL table names (aliases not implemented)");
    }
    JoinQuery on(const Expr& e) const {
        e.validate_join(left_, right_);
        JoinQuery q(*this);
        q.on_ = e; q.has_on_ = true;
        return q;
    }
    JoinQuery where(const Expr& e) const {
        e.validate_join(left_, right_);
        JoinQuery q(*this);
        q.where_ = q.has_where_ ? q.where_ && e : e;
        q.has_where_ = true;
        return q;
    }
    JoinQuery order_by(const Order& o) const {
        if (o.owner != left_ && o.owner != right_)
            throw std::invalid_argument("ORDER BY column not part of JOIN");
        JoinQuery q(*this); q.order_ = o.sql; return q;
    }
    JoinQuery limit(int n) const {
        if (n < 0) throw std::invalid_argument("negative LIMIT");
        JoinQuery q(*this); q.limit_ = n; return q;
    }
    SqlPlan compile(SqlDialect d = SqlDialect::SQLite) const {
        if (!has_on_) throw std::logic_error("INNER JOIN requires ON expression");
        if (!left_->size() || !right_->size())
            throw std::logic_error("JOIN requires both tables to have mapped columns");
        SqlPlan plan(d);
        plan.sql = "SELECT ";
        for (std::size_t i = 0; i < left_->size(); ++i) {
            if (i) plan.sql += ", ";
            plan.sql += quote_ident(left_->name()) + "." + quote_ident(left_->info(i).name);
        }
        for (std::size_t i = 0; i < right_->size(); ++i)
            plan.sql += ", " + quote_ident(right_->name()) + "." + quote_ident(right_->info(i).name);
        plan.sql += " FROM " + quote_ident(left_->name()) + " INNER JOIN " +
                    quote_ident(right_->name()) + " ON ";
        on_.append_to(plan);
        if (has_where_) { plan.sql += " WHERE "; where_.append_to(plan); }
        if (!order_.empty()) plan.sql += " ORDER BY " + order_;
        if (limit_ >= 0) {
            plan.sql += " LIMIT ";
            append_parameter(plan, ValueCodec<int>::encode(limit_));
        }
        return plan;
    }
private:
    const Table<Left>* left_;
    const Table<Right>* right_;
    bool has_on_, has_where_;
    Expr on_, where_;
    std::string order_;
    int limit_;
};


// Describes a typed SQL join. A LEFT joined model is represented by
// Optional<Model>, which stays empty when its non-null primary key is NULL.
template <typename Model, bool LeftOuter>
struct JoinedPart {
    typedef Model model_type;
    typedef typename std::conditional<LeftOuter, Optional<Model>, Model>::type result_type;
    static const bool left_outer = LeftOuter;
    const Table<Model>* table;
    Expr predicate;
    bool has_on;
    explicit JoinedPart(const Table<Model>& t) : table(&t), has_on(false) {}
};

// C++11 variadic JOIN chain. No raw SQL is accepted for join predicates.
// The first table is always present; subsequent tables can be INNER/LEFT joined.
template <typename First, typename... Parts>
class JoinChain {
public:
    JoinChain(const Table<First>& first, const std::tuple<Parts...>& parts)
        : first_(&first), parts_(parts), has_where_(false), limit_(-1) {
        if (sizeof...(Parts) == 0)
            throw std::invalid_argument("join chain requires at least one joined table");
        owners_.push_back(first_);
        table_names_.push_back(first_->name());
        initialize<0>();
    }

    JoinChain on(const Expr& e) const {
        e.validate_scope(owners_);
        JoinChain q(*this);
        std::get<sizeof...(Parts) - 1>(q.parts_).predicate = e;
        std::get<sizeof...(Parts) - 1>(q.parts_).has_on = true;
        return q;
    }
    JoinChain where(const Expr& e) const {
        e.validate_scope(owners_);
        JoinChain q(*this);
        q.where_ = q.has_where_ ? q.where_ && e : e;
        q.has_where_ = true;
        return q;
    }
    JoinChain order_by(const Order& order) const {
        if (std::find(owners_.begin(), owners_.end(), order.owner) == owners_.end())
            throw std::invalid_argument("ORDER BY column not part of JOIN");
        JoinChain q(*this); q.order_ = order.sql; return q;
    }
    JoinChain limit(int count) const {
        if (count < 0) throw std::invalid_argument("negative LIMIT");
        JoinChain q(*this); q.limit_ = count; return q;
    }

    template <typename Next>
    JoinChain<First, Parts..., JoinedPart<Next, false> >
    join(const Table<Next>& next) const { return append_join<Next, false>(next); }

    template <typename Next>
    JoinChain<First, Parts..., JoinedPart<Next, true> >
    left_join(const Table<Next>& next) const { return append_join<Next, true>(next); }

    const Table<First>& first_table() const { return *first_; }
    const std::tuple<Parts...>& parts() const { return parts_; }

    SqlPlan compile(SqlDialect dialect = SqlDialect::SQLite) const {
        if (!first_->size()) throw std::logic_error("JOIN requires mapped columns");
        SqlPlan plan(dialect);
        plan.sql = "SELECT ";
        append_columns(plan.sql, *first_, false);
        append_select_columns<0>(plan.sql);
        plan.sql += " FROM " + quote_ident(first_->name());
        append_joins<0>(plan);
        if (has_where_) { plan.sql += " WHERE "; where_.append_to(plan); }
        if (!order_.empty()) plan.sql += " ORDER BY " + order_;
        if (limit_ >= 0) {
            plan.sql += " LIMIT ";
            append_parameter(plan, ValueCodec<int>::encode(limit_));
        }
        return plan;
    }

private:
    template <typename Next, bool Outer>
    JoinChain<First, Parts..., JoinedPart<Next, Outer> >
    append_join(const Table<Next>& next) const {
        if (!last_join_ready())
            throw std::logic_error("previous JOIN requires ON before adding another table");
        JoinChain<First, Parts..., JoinedPart<Next, Outer> > q(
            *first_, std::tuple_cat(parts_, std::make_tuple(JoinedPart<Next, Outer>(next))));
        q.set_filter(has_where_, where_, order_, limit_);
        return q;
    }
    // Called only by append_join() across different specializations.
    void set_filter(bool has_where, const Expr& where, const std::string& order, int limit) {
        has_where_ = has_where; where_ = where; order_ = order; limit_ = limit;
    }
    template <typename, typename...> friend class JoinChain;

    template <typename Model>
    static void append_columns(std::string& sql, const Table<Model>& table, bool separator) {
        for (std::size_t i = 0; i < table.size(); ++i) {
            if (i || separator) sql += ", ";
            sql += quote_ident(table.name()) + "." + quote_ident(table.info(i).name);
        }
    }
    template <std::size_t I>
    typename std::enable_if<I == sizeof...(Parts), void>::type initialize() {}
    template <std::size_t I>
    typename std::enable_if<I < sizeof...(Parts), void>::type initialize() {
        const auto& part = std::get<I>(parts_);
        if (!part.table->size()) throw std::invalid_argument("joined table has no mapped columns");
        if (std::find(table_names_.begin(), table_names_.end(), part.table->name()) != table_names_.end())
            throw std::invalid_argument("JOIN requires distinct table names; aliases not supported");
        if (part.left_outer && part.table->primary_index() < 0)
            throw std::invalid_argument("LEFT JOIN requires the joined table to have a primary key");
        owners_.push_back(part.table);
        table_names_.push_back(part.table->name());
        initialize<I+1>();
    }
    bool last_join_ready() const {
        return std::get<sizeof...(Parts) - 1>(parts_).has_on;
    }
    template <std::size_t I>
    typename std::enable_if<I == sizeof...(Parts), void>::type
    append_select_columns(std::string&) const {}
    template <std::size_t I>
    typename std::enable_if<I < sizeof...(Parts), void>::type
    append_select_columns(std::string& sql) const {
        append_columns(sql, *std::get<I>(parts_).table, true);
        append_select_columns<I+1>(sql);
    }
    template <std::size_t I>
    typename std::enable_if<I == sizeof...(Parts), void>::type
    append_joins(SqlPlan&) const {}
    template <std::size_t I>
    typename std::enable_if<I < sizeof...(Parts), void>::type
    append_joins(SqlPlan& plan) const {
        const auto& part = std::get<I>(parts_);
        if (!part.has_on) throw std::logic_error("JOIN requires ON expression");
        plan.sql += part.left_outer ? " LEFT JOIN " : " INNER JOIN ";
        plan.sql += quote_ident(part.table->name()) + " ON ";
        part.predicate.append_to(plan);
        append_joins<I+1>(plan);
    }
    const Table<First>* first_;
    std::tuple<Parts...> parts_;
    std::vector<const void*> owners_;
    std::vector<std::string> table_names_;
    bool has_where_;
    Expr where_;
    std::string order_;
    int limit_;
};

// Relation metadata: both columns must have the exact same C++ key type.
template <typename Parent, typename Child, typename Key>
class HasMany {
public:
    HasMany(const Table<Parent>& parent, const Column<Parent,Key>& parent_key,
            const Table<Child>& child, const Column<Child,Key>& child_key)
        : parent_(&parent), parent_key_(parent_key), child_(&child), child_key_(child_key) {
        parent.validate(parent_key);
        child.validate(child_key);
    }
    const Table<Parent>& parent() const { return *parent_; }
    const Table<Child>& child() const { return *child_; }
    const Column<Parent,Key>& parent_key() const { return parent_key_; }
    const Column<Child,Key>& child_key() const { return child_key_; }
private:
    const Table<Parent>* parent_;
    Column<Parent,Key> parent_key_;
    const Table<Child>* child_;
    Column<Child,Key> child_key_;
};

template <typename Parent, typename Child, typename Key>
HasMany<Parent, Child, Key> has_many(
    const Table<Parent>& parent, const Column<Parent,Key>& pk,
    const Table<Child>& child, const Column<Child,Key>& fk) {
    return HasMany<Parent, Child, Key>(parent, pk, child, fk);
}

template <typename Child, typename Parent, typename Key>
class BelongsTo {
public:
    BelongsTo(const Table<Child>& child, const Column<Child,Key>& fk,
              const Table<Parent>& parent, const Column<Parent,Key>& pk)
        : child_(&child), fk_(fk), parent_(&parent), pk_(pk) {
        child.validate(fk);
        parent.validate(pk);
        if (parent.primary_index() != static_cast<int>(pk.index()))
            throw std::invalid_argument("belongs_to must reference the parent primary key");
    }
    const Table<Child>& child() const { return *child_; }
    const Table<Parent>& parent() const { return *parent_; }
    const Column<Child,Key>& foreign_key() const { return fk_; }
    const Column<Parent,Key>& primary_key() const { return pk_; }
private:
    const Table<Child>* child_;
    Column<Child,Key> fk_;
    const Table<Parent>* parent_;
    Column<Parent,Key> pk_;
};

template <typename Child, typename Parent, typename Key>
BelongsTo<Child,Parent,Key> belongs_to(
    const Table<Child>& child, const Column<Child,Key>& fk,
    const Table<Parent>& parent, const Column<Parent,Key>& pk) {
    return BelongsTo<Child,Parent,Key>(child, fk, parent, pk);
}

} // namespace liteorm
#endif
