#ifndef LITEORM_REPOSITORY_HPP
#define LITEORM_REPOSITORY_HPP
#include "liteorm/core.hpp"
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <vector>
namespace liteorm {
template<class M> class Predicate {
    std::function<Expr(const Table<M>&)> bind_;
public:
    explicit Predicate(const std::function<Expr(const Table<M>&)>& f):bind_(f){}
    Expr bind(const Table<M>& t) const {return bind_(t);}
    friend Predicate operator&&(const Predicate& a,const Predicate& b) {
        return Predicate([a,b](const Table<M>& t){return a.bind(t)&&b.bind(t);});
    }
    friend Predicate operator||(const Predicate& a,const Predicate& b) {
        return Predicate([a,b](const Table<M>& t){return a.bind(t)||b.bind(t);});
    }
    friend Predicate operator!(const Predicate& a) {
        return Predicate([a](const Table<M>& t){return !a.bind(t);});
    }
};
template<class M,class F> class Property {
    F M::* member_;
    Predicate<M> cmp(const char* op,const F& v) const {
        const F M::* unused=NULL; (void)unused;
        F M::* member=member_; const std::string operation(op);
        return Predicate<M>([member,operation,v](const Table<M>& t){
            const Column<M,F> c=t.column(member);
            const Value value=ValueCodec<F>::encode(v);
            if(value.kind==Value::Null) {
                if(operation=="=") return c.is_null();
                if(operation=="<>") return c.is_not_null();
                throw std::invalid_argument("NULL supports == and != only");
            }
            return Expr::compare(&t,c.qualified_name(),operation.c_str(),value);
        });
    }
public:
    explicit Property(F M::* member):member_(member) {
        if(!member) throw std::invalid_argument("null member pointer");
    }
    Predicate<M> operator==(const F& v) const{return cmp("=",v);}
    Predicate<M> operator!=(const F& v) const{return cmp("<>",v);}
    Predicate<M> operator<(const F& v) const{return cmp("<",v);}
    Predicate<M> operator<=(const F& v) const{return cmp("<=",v);}
    Predicate<M> operator>(const F& v) const{return cmp(">",v);}
    Predicate<M> operator>=(const F& v) const{return cmp(">=",v);}
    Predicate<M> is_null() const {
        F M::* p=member_;
        return Predicate<M>([p](const Table<M>& t){return t.column(p).is_null();});
    }
    Predicate<M> is_not_null() const {
        F M::* p=member_;
        return Predicate<M>([p](const Table<M>& t){return t.column(p).is_not_null();});
    }
    Predicate<M> in(const std::vector<F>& values) const {
        F M::* p=member_;
        return Predicate<M>([p,values](const Table<M>& t){return t.column(p).in(values);});
    }
};
template<class M,class F> Property<M,F> col(F M::* p){return Property<M,F>(p);}
template<class M> class ModelBuilder {
    std::shared_ptr<Table<M> > table_;
public:
    explicit ModelBuilder(const std::shared_ptr<Table<M> >& t):table_(t){}
    template<class F> ModelBuilder& primary_key(const std::string& name,F M::* p,
                                                  bool generated=true) {
        table_->add(name,p,generated?ColumnOptions::identity():ColumnOptions::primary());
        return *this;
    }
    template<class F> ModelBuilder& field(const std::string& name,F M::* p,
                                          bool unique=false) {
        table_->add(name,p,unique?ColumnOptions::unique_key():ColumnOptions());
        return *this;
    }
};
template<class M> class RepositoryQuery {
    std::shared_ptr<Table<M> > table_;
    BoundSelect<M> q_;
public:
    RepositoryQuery(Database& db,const std::shared_ptr<Table<M> >& t)
        :table_(t),q_(db.select(*t)){}
    RepositoryQuery where(const Predicate<M>& p) const {
        RepositoryQuery q(*this);q.q_=q.q_.where(p.bind(*table_));return q;
    }
    RepositoryQuery where(const Expr& p) const {
        RepositoryQuery q(*this);q.q_=q.q_.where(p);return q;
    }
    template<class F> RepositoryQuery order_by(F M::* p) const {
        RepositoryQuery q(*this);q.q_=q.q_.order_by(table_->column(p).asc());return q;
    }
    template<class F> RepositoryQuery order_by_desc(F M::* p) const {
        RepositoryQuery q(*this);q.q_=q.q_.order_by(table_->column(p).desc());return q;
    }
    RepositoryQuery limit(int n) const {
        RepositoryQuery q(*this);q.q_=q.q_.limit(n);return q;
    }
    std::vector<M> all() const{return q_.all();}
    Optional<M> first() const {
        std::unique_ptr<M> v=q_.first();
        return v?Optional<M>(*v):Optional<M>();
    }
    std::int64_t count() const{return q_.count();}
    bool exists() const{return q_.exists();}
};
template<class M> class Repository {
    Database* db_;
    std::shared_ptr<Table<M> > table_;
    int pk() const {
        int i=table_->primary_index();
        if(i<0) throw std::logic_error("primary key not registered");
        return i;
    }
public:
    Repository(Database& db,const std::shared_ptr<Table<M> >& t):db_(&db),table_(t){}
    void create_table(bool if_not_exists=true){db_->create_table(*table_,if_not_exists);}
    std::int64_t create(M& model) {
        std::int64_t id=db_->insert(*table_,model);
        int i=table_->primary_index();
        if(i>=0&&table_->info(static_cast<std::size_t>(i)).generated)
            table_->assign(static_cast<std::size_t>(i),model,Value(id));
        return id;
    }
    template<class K> Optional<M> find(const K& id) const {
        int i=pk();Value key=ValueCodec<K>::encode(id);
        if(key.kind==Value::Null) throw std::invalid_argument("null primary key");
        Expr e=Expr::compare(table_.get(),quote_ident(table_->name())+"."+
            quote_ident(table_->info(static_cast<std::size_t>(i)).name),"=",key);
        return where(e).first();
    }
    std::vector<M> find_all() const{return db_->select(*table_).all();}
    template<class F,class V> std::vector<M> find_by(F M::* p,const V& v) const {
        F value(v);const Column<M,F> c=table_->column(p);
        const Value encoded=ValueCodec<F>::encode(value);
        return db_->select(*table_).where(encoded.kind==Value::Null?
            c.is_null():c==value).all();
    }
    int update(const M& model) {
        int i=pk();Value key=table_->extract(static_cast<std::size_t>(i),model);
        if(key.kind==Value::Null) throw std::invalid_argument("null primary key");
        SqlPlan plan(db_->dialect());
        plan.sql="UPDATE "+quote_ident(table_->name())+" SET ";
        bool any=false;
        for(std::size_t x=0;x<table_->size();++x) {
            if(static_cast<int>(x)==i||table_->info(x).generated)continue;
            if(any)plan.sql+=", ";
            any=true;plan.sql+=quote_ident(table_->info(x).name)+" = ";
            append_parameter(plan,table_->extract(x,model));
        }
        if(!any)throw std::logic_error("UPDATE needs non-key fields");
        plan.sql+=" WHERE "+quote_ident(table_->info(static_cast<std::size_t>(i)).name)+" = ";
        append_parameter(plan,key);return db_->execute(plan);
    }
    template<class K> int remove(const K& id) {
        int i=pk();Value key=ValueCodec<K>::encode(id);
        if(key.kind==Value::Null)throw std::invalid_argument("null primary key");
        SqlPlan plan(db_->dialect());
        plan.sql="DELETE FROM "+quote_ident(table_->name())+" WHERE "+
            quote_ident(table_->info(static_cast<std::size_t>(i)).name)+" = ";
        append_parameter(plan,key);return db_->execute(plan);
    }
    std::int64_t count() const{return db_->select(*table_).count();}
    bool exists() const{return db_->select(*table_).exists();}
    template<class F> Column<M,F> column(F M::* p) const{return table_->column(p);}
    RepositoryQuery<M> query() const{return RepositoryQuery<M>(*db_,table_);}
    RepositoryQuery<M> where(const Predicate<M>& p) const{return query().where(p);}
    RepositoryQuery<M> where(const Expr& p) const{return query().where(p);}
    template<class F> RepositoryQuery<M> order_by(F M::* p) const {
        return query().order_by(p);
    }
};
template<class M> ModelBuilder<M> Database::model(const std::string& name) {
    std::type_index key(typeid(M));
    if(models_.count(key))throw std::logic_error("model already registered");
    std::shared_ptr<Table<M> > t(new Table<M>(name));
    models_.insert(std::make_pair(key,std::static_pointer_cast<void>(t)));
    return ModelBuilder<M>(t);
}
template<class M> Repository<M> Database::repository() {
    typename std::map<std::type_index,std::shared_ptr<void> >::iterator
        it=models_.find(std::type_index(typeid(M)));
    if(it==models_.end())throw std::logic_error("model is not registered");
    return Repository<M>(*this,std::static_pointer_cast<Table<M> >(it->second));
}
} // namespace liteorm
#endif
