#include "liteorm/liteorm.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <string>
#include <iostream>
#include <type_traits>
#include <utility>
struct User {
    std::int64_t id;
    std::string name;
    int age;
    liteorm::Optional<std::string> note;
    User():id(0),age(0){}
};
struct MoveProbe {
    std::int64_t id;
    std::string label;
    MoveProbe() : id(0) {}
};
struct ManualKey {
    std::string id;
    std::string name;
};
int main(int argc,char** argv) {
    bool pg=argc>1&&std::string(argv[1])=="--postgresql";
    const char* dsn=std::getenv("LITEORM_TEST_PG_DSN");
    if(pg&&(!dsn||!*dsn)) {
        const char* required=std::getenv("LITEORM_REQUIRE_PG");
        return required&&std::string(required)=="1"?1:77;
    }
    static_assert(std::is_same<
        decltype(std::declval<liteorm::Repository<User>&>().create(
            std::declval<User&>())), void>::value,
        "Repository::create(User&) must return void");
    static_assert(std::is_same<
        decltype(std::declval<liteorm::Repository<ManualKey>&>().create(
            std::declval<ManualKey&>())), void>::value,
        "Repository::create(ManualKey&) must return void");
    static_assert(!std::is_copy_constructible<liteorm::Database>::value,
                  "Database stays non-copyable");
    static_assert(std::is_move_constructible<liteorm::Database>::value,
                  "C++11 factories need move construction");
    static_assert(!std::is_move_assignable<liteorm::Database>::value,
                  "Database does not support move assignment");
    liteorm::Database db = pg
        ? liteorm::Database::postgresql(dsn)
        : liteorm::Database::sqlite(":memory:");
    assert(db.backend_kind()==
        (pg ? liteorm::BackendKind::PostgreSQL : liteorm::BackendKind::SQLite));
    const std::string name="liteorm_repo_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string probe_name=name+"_move_probe";
    db.model<MoveProbe>(probe_name).primary_key("id",&MoveProbe::id)
        .field("label",&MoveProbe::label);
    // Validate moving the driver and model registry before creating any handles.
    liteorm::Database moved(std::move(db));
    auto probes=moved.repository<MoveProbe>();
    probes.create_table(false);
    MoveProbe probe;probe.label="moved registry";
    probes.create(probe);
    assert(probe.id>0 && probes.find(probe.id).has_value());
    assert(probes.remove(probe.id)==1);
    moved.execute_sql("DROP TABLE "+liteorm::quote_ident(probe_name));
    moved.model<User>(name).primary_key("id",&User::id)
        .field("name",&User::name,true).field("age",&User::age)
        .field("note",&User::note);
    auto users=moved.repository<User>();
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
        {liteorm::Transaction tx=moved.transaction();users.remove(a.id);}
        assert(users.count()==1);
        assert(users.remove(a.id)==1);
    }catch(...){
        moved.execute_sql("DROP TABLE IF EXISTS "+liteorm::quote_ident(name));throw;
    }
    moved.execute_sql("DROP TABLE "+liteorm::quote_ident(name));

    // The same create(T&) contract must hold for manually assigned string
    // primary keys on both SQLite and live PostgreSQL.
    const std::string manual_table = name + "_manual";
    moved.model<ManualKey>(manual_table)
        .primary_key("id", &ManualKey::id, false)
        .field("name", &ManualKey::name);
    auto manual = moved.repository<ManualKey>();
    manual.create_table(false);
    ManualKey first;
    first.id = "account-one";
    first.name = "first";
    manual.create(first);
    assert(first.id == "account-one");
    assert(manual.find(std::string("account-one")).has_value());
    assert(manual.count() == 1);

    // Failure is signaled by an exception, never an ID sentinel.
    ManualKey duplicate = first;
    bool rejected_duplicate = false;
    try {
        manual.create(duplicate);
    } catch (const std::exception&) {
        rejected_duplicate = true;
    }
    assert(rejected_duplicate);
    assert(manual.count() == 1);
    assert(manual.remove(std::string("account-one")) == 1);
    moved.execute_sql("DROP TABLE "+liteorm::quote_ident(manual_table));

    if(!pg) {
        liteorm::Database legacy(":memory:");
        assert(legacy.backend_kind()==liteorm::BackendKind::SQLite);
    }
#if !LITEORM_HAS_POSTGRESQL
    bool unsupported_throws=false;
    try {
        liteorm::Database unsupported=liteorm::Database::postgresql("host=127.0.0.1");
        (void)unsupported;
    } catch(const std::runtime_error&) { unsupported_throws=true; }
    assert(unsupported_throws);
#endif
    std::cout<<"Object-first repository integration OK\n";
}
