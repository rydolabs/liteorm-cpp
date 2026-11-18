#include "liteorm/liteorm.hpp"
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

struct Account {
    std::int64_t id;
    std::string name;
    std::string other;
    Account() : id(0) {}
};
struct Pending {
    std::int64_t id;
    std::string label;
    Pending() : id(0) {}
};
struct StringKey {
    std::string id;
    std::string name;
};

template <class Func>
static void must_logic_error(const Func& action) {
    bool caught = false;
    try { action(); } catch (const std::logic_error&) { caught = true; }
    assert(caught);
}
template <class Func>
static void must_invalid_argument(const Func& action) {
    bool caught = false;
    try { action(); } catch (const std::invalid_argument&) { caught = true; }
    assert(caught);
}

int main() {
    static_assert(std::is_same<
        decltype(std::declval<liteorm::Repository<Account>&>().create(
            std::declval<Account&>())), void>::value,
        "create() should not conflate affected rows and integer primary keys");

    liteorm::Database db = liteorm::Database::sqlite(":memory:");
    db.model<Account>("accounts")
        .primary_key("id", &Account::id).field("name", &Account::name);
    {
        auto users = db.repository<Account>();
        users.create_table();
        Account alice; alice.name = "Alice";
        users.create(alice);
        assert(alice.id > 0);
        must_logic_error([&]() {
            liteorm::Database moved(std::move(db));
            (void)moved;
        });
        assert(users.find(alice.id).has_value());
        {
            liteorm::Transaction tx = db.transaction();
            must_logic_error([&]() {
                liteorm::Database moved(std::move(db)); (void)moved;
            });
            tx.rollback();
        }
        assert(users.count() == 1);
    }
    // All dependent handles are gone, so moving is safe and preserves metadata.
    liteorm::Database moved(std::move(db));
    must_logic_error([&]() { (void)db.dialect(); });
    auto users = moved.repository<Account>();
    assert(users.count() == 1);
    {
        liteorm::SqlPlan plan(moved.dialect());
        plan.sql = "SELECT 1";
        liteorm::Statement stmt(moved, plan);
        must_logic_error([&]() {
            liteorm::Database second(std::move(moved)); (void)second;
        });
        assert(stmt.step());
    }

    // Missing key type is not a sentinel for errors: create() uses exceptions.
    moved.model<StringKey>("strings")
        .primary_key("id", &StringKey::id, false)
        .field("name", &StringKey::name);
    auto strings = moved.repository<StringKey>();
    strings.create_table();
    StringKey s; s.id = "key-1"; s.name = "manual";
    strings.create(s);
    assert(strings.find(std::string("key-1")).has_value());

    // An exception in the registration chain must not expose partial metadata.
    must_invalid_argument([&]() {
        moved.model<Pending>("partial")
            .primary_key("id", &Pending::id)
            .field("id", &Pending::label); // duplicate SQL column
    });
    must_logic_error([&]() { (void)moved.repository<Pending>(); });
    moved.model<Pending>("pending")
        .primary_key("id", &Pending::id)
        .field("label", &Pending::label);
    auto pending = moved.repository<Pending>();
    pending.create_table();
    Pending p; p.label = "registered";
    pending.create(p);
    assert(p.id > 0);

    // A builder that is not yet finished cannot be used by a repository.
    struct Deferred { std::int64_t id; std::string name; Deferred() : id(0) {} };
    {
        auto builder = moved.model<Deferred>("deferred");
        builder.primary_key("id", &Deferred::id).field("name", &Deferred::name);
        must_logic_error([&]() { (void)moved.repository<Deferred>(); });
    }
    auto deferred = moved.repository<Deferred>();
    deferred.create_table();

    std::unique_ptr<liteorm::Repository<Account> > escaped;
    std::unique_ptr<liteorm::RepositoryQuery<Account> > escaped_query;
    {
        std::unique_ptr<liteorm::Database> scoped(
            new liteorm::Database(liteorm::Database::sqlite(":memory:")));
        scoped->model<Account>("scoped")
            .primary_key("id", &Account::id).field("name", &Account::name);
        escaped.reset(new liteorm::Repository<Account>(scoped->repository<Account>()));
        escaped_query.reset(new liteorm::RepositoryQuery<Account>(escaped->query()));
        escaped->create_table();
    }
    must_logic_error([&]() { (void)escaped->find_all(); });
    must_logic_error([&]() { (void)escaped_query->all(); });
}
