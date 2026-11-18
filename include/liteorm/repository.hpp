#ifndef LITEORM_REPOSITORY_HPP
#define LITEORM_REPOSITORY_HPP

// Normally included by liteorm/database.hpp, after Database and BoundSelect.
#include "liteorm/core.hpp"
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <vector>

namespace liteorm {

template <typename Model>
class Predicate {
public:
    explicit Predicate(const std::function<Expr(const Table<Model>&)>& binder)
        : bind_(binder) {}
    Expr bind(const Table<Model>& table) const { return bind_(table); }
    friend Predicate operator&&(const Predicate& a, const Predicate& b) {
        return Predicate([a,b](const Table<Model>& table) {
            return a.bind(table) && b.bind(table);
        });
    }
    friend Predicate operator||(const Predicate& a, const Predicate& b) {
        return Predicate([a,b](const Table<Model>& table) {
            return a.bind(table) || b.bind(table);
        });
    }
    friend Predicate operator!(const Predicate& e) {
        return Predicate([e](const Table<Model>& table) { return !e.bind(table); });
    }
private:
    std::function<Expr(const Table<Model>&)> bind_;
};

template <typename Model, typename Field>
class Property {
public:
    explicit Property(Field Model::* member) : member_(member) {
        if (!member) throw std::invalid_argument("null model member pointer");
    }
    Predicate<Model> operator==(const Field& v) const { return compare("=", v); }
    Predicate<Model> operator!=(const Field& v) const { return compare("<>", v); }
    Predicate<Model> operator<(const Field& v) const { return compare("<", v); }
    Predicate<Model> operator<=(const Field& v) const { return compare("<=", v); }
    Predicate<Model> operator>(const Field& v) const { return compare(">", v); }
    Predicate<Model> operator>=(const Field& v) const { return compare(">=", v); }
    Predicate<Model> is_null() const {
        Field Model::* member = member_;
        return Predicate<Model>([member](const Table<Model>& t) {
            return t.column(member).is_null();
        });
    }
    Predicate<Model> is_not_null() const {
        Field Model::* member = member_;
        return Predicate<Model>([member](const Table<Model>& t) {
            return t.column(member).is_not_null();
        });
    }
    Predicate<Model> in(const std::vector<Field>& values) const {
        Field Model::* member = member_;
        return Predicate<Model>([member,values](const Table<Model>& t) {
            return t.column(member).in(values);
        });
    }
private:
    Field Model::* member_;
    Predicate<Model> compare(const char* op, const Field& value) const {
        Field Model::* member = member_;
        const std::string operation(op);
        return Predicate<Model>([member,operation,value](const Table<Model>& t) {
            const Column<Model,Field> field = t.column(member);
            const Value encoded = ValueCodec<Field>::encode(value);
            if (encoded.kind == Value::Null) {
                if (operation == "=") return field.is_null();
                if (operation == "<>") return field.is_not_null();
                throw std::invalid_argument("NULL supports only equality and inequality");
            }
            return Expr::compare(&t, field.qualified_name(), operation.c_str(), encoded);
        });
    }
};

template <typename Model, typename Field>
Property<Model,Field> col(Field Model::* member) { return Property<Model,Field>(member); }

template <typename Model>
class ModelBuilder {
    struct Draft {
        std::shared_ptr<Table<Model> > table;
        std::shared_ptr<detail::Registration> status;
        Draft(const std::shared_ptr<Table<Model> >& t,
              const std::shared_ptr<detail::Registration>& r) : table(t), status(r) {}
        ~Draft() {
            if (!status->failed && table->size() > 0)
                status->ready = true;
        }
    };
    std::shared_ptr<Draft> draft_;
public:
    ModelBuilder(const std::shared_ptr<Table<Model> >& table,
                 const std::shared_ptr<detail::Registration>& status)
        : draft_(new Draft(table, status)) {}
    template <typename Field>
    ModelBuilder& primary_key(const std::string& name, Field Model::* member,
                              bool generated = true) {
        try {
            draft_->table->add(name, member,
                generated ? ColumnOptions::identity() : ColumnOptions::primary());
        } catch (...) { draft_->status->failed = true; throw; }
        return *this;
    }
    template <typename Field>
    ModelBuilder& field(const std::string& name, Field Model::* member, bool unique = false) {
        try {
            draft_->table->add(name, member,
                unique ? ColumnOptions::unique_key() : ColumnOptions());
        } catch (...) { draft_->status->failed = true; throw; }
        return *this;
    }
};

template <typename Model>
class RepositoryQuery {
public:
    RepositoryQuery(Database& db, const std::shared_ptr<Table<Model> >& table)
        : table_(table), query_(db.select(*table)), lifetime_(db.lifetime_) {}
    RepositoryQuery where(const Predicate<Model>& condition) const {
        RepositoryQuery q(*this); q.query_ = q.query_.where(condition.bind(*table_)); return q;
    }
    RepositoryQuery where(const Expr& condition) const {
        RepositoryQuery q(*this); q.query_ = q.query_.where(condition); return q;
    }
    template <typename Field>
    RepositoryQuery order_by(Field Model::* member) const {
        RepositoryQuery q(*this); q.query_ = q.query_.order_by(table_->column(member).asc()); return q;
    }
    template <typename Field>
    RepositoryQuery order_by_desc(Field Model::* member) const {
        RepositoryQuery q(*this); q.query_ = q.query_.order_by(table_->column(member).desc()); return q;
    }
    RepositoryQuery limit(int n) const {
        RepositoryQuery q(*this); q.query_ = q.query_.limit(n); return q;
    }
    std::vector<Model> all() const { check_live(); return query_.all(); }
    Optional<Model> first() const {
        check_live();
        std::unique_ptr<Model> result = query_.first();
        return result ? Optional<Model>(*result) : Optional<Model>();
    }
    std::int64_t count() const { check_live(); return query_.count(); }
    bool exists() const { check_live(); return query_.exists(); }
private:
    std::shared_ptr<Table<Model> > table_;
    BoundSelect<Model> query_;
    std::shared_ptr<detail::DatabaseLifetime> lifetime_;
    void check_live() const {
        if (!lifetime_ || !lifetime_->alive)
            throw std::logic_error("Repository query used after Database destruction");
    }
};

template <typename Model>
class Repository {
public:
    Repository(Database& db, const std::shared_ptr<Table<Model> >& table)
        : db_(&db), table_(table), lifetime_(db.lifetime_) {}
    void create_table(bool if_not_exists = true) {
        check_live(); db_->create_table(*table_, if_not_exists);
    }
    // Inserting a generated integer key writes the database-assigned ID back
    // to the C++ object; there is intentionally no change tracking.
    void create(Model& value) {
        check_live();
        const std::int64_t id = db_->insert(*table_, value);
        const int pk = table_->primary_index();
        if (pk >= 0 && table_->info(static_cast<std::size_t>(pk)).generated)
            table_->assign(static_cast<std::size_t>(pk), value, Value(id));
    }
    template <typename Key>
    Optional<Model> find(const Key& id) const {
        const int pk = require_pk();
        const Value value = ValueCodec<Key>::encode(id);
        if (value.kind == Value::Null) throw std::invalid_argument("null primary key");
        const std::string column_name = table_->info(static_cast<std::size_t>(pk)).name;
        const Expr condition = Expr::compare(table_.get(),
            quote_ident(table_->name()) + "." + quote_ident(column_name), "=", value);
        return where(condition).first();
    }
    std::vector<Model> find_all() const {
        check_live(); return db_->select(*table_).all();
    }
    template <typename Field, typename ValueLike>
    std::vector<Model> find_by(Field Model::* member, const ValueLike& value) const {
        check_live();
        const Field converted(value);
        const Column<Model,Field> field = table_->column(member);
        const Value encoded = ValueCodec<Field>::encode(converted);
        return db_->select(*table_).where(encoded.kind == Value::Null ?
            field.is_null() : field == converted).all();
    }
    int update(const Model& value) {
        const int pk = require_pk();
        const Value key = table_->extract(static_cast<std::size_t>(pk), value);
        if (key.kind == Value::Null) throw std::invalid_argument("null primary key");
        SqlPlan plan(db_->dialect());
        plan.sql = "UPDATE " + quote_ident(table_->name()) + " SET ";
        bool has_set = false;
        for (std::size_t i = 0; i < table_->size(); ++i) {
            if (static_cast<int>(i) == pk || table_->info(i).generated) continue;
            if (has_set) plan.sql += ", ";
            has_set = true;
            plan.sql += quote_ident(table_->info(i).name) + " = ";
            append_parameter(plan, table_->extract(i, value));
        }
        if (!has_set) throw std::logic_error("UPDATE needs non-key fields");
        plan.sql += " WHERE " + quote_ident(table_->info(static_cast<std::size_t>(pk)).name) + " = ";
        append_parameter(plan, key);
        return db_->execute(plan);
    }
    template <typename Key>
    int remove(const Key& id) {
        const int pk = require_pk();
        const Value key = ValueCodec<Key>::encode(id);
        if (key.kind == Value::Null) throw std::invalid_argument("null primary key");
        SqlPlan plan(db_->dialect());
        plan.sql = "DELETE FROM " + quote_ident(table_->name()) + " WHERE " +
            quote_ident(table_->info(static_cast<std::size_t>(pk)).name) + " = ";
        append_parameter(plan, key);
        return db_->execute(plan);
    }
    std::int64_t count() const { check_live(); return db_->select(*table_).count(); }
    bool exists() const { check_live(); return db_->select(*table_).exists(); }
    template <typename Field>
    Column<Model,Field> column(Field Model::* member) const {
        check_live(); return table_->column(member);
    }
    RepositoryQuery<Model> query() const {
        check_live(); return RepositoryQuery<Model>(*db_, table_);
    }
    RepositoryQuery<Model> where(const Predicate<Model>& p) const { return query().where(p); }
    RepositoryQuery<Model> where(const Expr& p) const { return query().where(p); }
    template <typename Field>
    RepositoryQuery<Model> order_by(Field Model::* member) const {
        return query().order_by(member);
    }
private:
    void check_live() const {
        if (!lifetime_ || !lifetime_->alive)
            throw std::logic_error("Repository used after Database destruction");
    }
    int require_pk() const {
        check_live();
        const int pk = table_->primary_index();
        if (pk < 0) throw std::logic_error("Repository requires a primary key");
        return pk;
    }
    Database* db_;
    std::shared_ptr<Table<Model> > table_;
    std::shared_ptr<detail::DatabaseLifetime> lifetime_;
};

template <typename Model>
ModelBuilder<Model> Database::model(const std::string& table_name) {
    if (!lifetime_ || !lifetime_->alive)
        throw std::logic_error("using moved-from Database");
    const std::type_index key(typeid(Model));
    const std::map<std::type_index, std::shared_ptr<detail::Registration> >::iterator
        existing = model_status_.find(key);
    if (existing != model_status_.end() && existing->second->ready)
        throw std::logic_error("model already registered");
    std::shared_ptr<Table<Model> > table(new Table<Model>(table_name));
    std::shared_ptr<detail::Registration> status(new detail::Registration());
    models_[key] = std::static_pointer_cast<void>(table);
    model_status_[key] = status;
    return ModelBuilder<Model>(table, status);
}

template <typename Model>
Repository<Model> Database::repository() {
    if (!lifetime_ || !lifetime_->alive)
        throw std::logic_error("using moved-from Database");
    const std::type_index key(typeid(Model));
    const std::map<std::type_index,std::shared_ptr<void> >::iterator it = models_.find(key);
    const std::map<std::type_index, std::shared_ptr<detail::Registration> >::iterator
        status = model_status_.find(key);
    if (it == models_.end() || status == model_status_.end() || !status->second->ready)
        throw std::logic_error("model is not registered or registration is incomplete");
    return Repository<Model>(*this, std::static_pointer_cast<Table<Model> >(it->second));
}

} // namespace liteorm
#endif
