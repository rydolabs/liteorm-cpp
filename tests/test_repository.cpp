#include "liteorm/liteorm.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <string>
#include <iostream>
struct User {
    std::int64_t id;
    std::string name;
    int age;
    liteorm::Optional<std::string> note;
    User():id(0),age(0){}
};
int main(int argc,char** argv) {
    bool pg=argc>1&&std::string(argv[1])=="--postgresql";
    const char* dsn=std::getenv("LITEORM_TEST_PG_DSN");
    if(pg&&(!dsn||!*dsn)) {
        const char* required=std::getenv("LITEORM_REQUIRE_PG");
        return required&&std::string(required)=="1"?1:77;
    }
    liteorm::Database db(pg?dsn:":memory:",
        pg?liteorm::BackendKind::PostgreSQL:liteorm::BackendKind::SQLite);
    const std::string name="liteorm_repo_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    db.model<User>(name).primary_key("id",&User::id)
        .field("name",&User::name,true).field("age",&User::age)
        .field("note",&User::note);
    auto users=db.repository<User>();
    users.create_table(false);
    try {
        User a;a.name="Alice' OR 1=1 --";a.age=27;a.note=std::string(u8"한글");
        users.create(a);assert(a.id>0);
        User b;b.name="Bob";b.age=16;users.create(b);
        assert(users.count()==2);
        assert(users.find(a.id).has_value());
        assert(!users.find(99999).has_value());
        assert(users.find_by(&User::name,"Bob").size()==1);
        assert(users.find_by(&User::note,liteorm::Optional<std::string>()).size()==1);
        assert(users.where(liteorm::col(&User::age)>=18).count()==1);
        assert(users.where(liteorm::col(&User::note).is_not_null()).exists());
        assert(users.where(liteorm::col(&User::age)>10&&
            liteorm::col(&User::age)<30).order_by(&User::name).limit(1).all().size()==1);
        a.age=28;a.note.reset();
        assert(users.update(a)==1);
        assert(users.find(a.id).value().age==28);
        assert(users.remove(b.id)==1);
        {liteorm::Transaction tx=db.transaction();users.remove(a.id);}
        assert(users.count()==1);
        assert(users.remove(a.id)==1);
    }catch(...){
        db.execute_sql("DROP TABLE IF EXISTS "+liteorm::quote_ident(name));throw;
    }
    db.execute_sql("DROP TABLE "+liteorm::quote_ident(name));
    std::cout<<"Object-first repository integration OK\n";
}
