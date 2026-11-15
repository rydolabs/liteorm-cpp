#include "liteorm/liteorm.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

struct User { std::int64_t id; std::string name; User() : id(0) {} };
struct Post { std::int64_t id, user_id; std::string title;
    Post() : id(0), user_id(0) {} };
struct Comment { std::int64_t id, post_id; std::string body;
    Comment() : id(0), post_id(0) {} };
struct Users : liteorm::Table<User> {
    liteorm::Column<User, std::int64_t> id;
    liteorm::Column<User, std::string> name;
    Users() : Table("users"),
        id(add("id", &User::id, liteorm::ColumnOptions::identity())),
        name(add("name", &User::name)) {}
};
struct Posts : liteorm::Table<Post> {
    liteorm::Column<Post, std::int64_t> id;
    liteorm::Column<Post, std::int64_t> user_id;
    liteorm::Column<Post, std::string> title;
    Posts() : Table("posts"),
        id(add("id", &Post::id, liteorm::ColumnOptions::identity())),
        user_id(add("user_id", &Post::user_id,
                    liteorm::ColumnOptions::foreign_key("users", "id"))),
        title(add("title", &Post::title)) {}
};
struct Comments : liteorm::Table<Comment> {
    liteorm::Column<Comment, std::int64_t> id;
    liteorm::Column<Comment, std::int64_t> post_id;
    liteorm::Column<Comment, std::string> body;
    Comments() : Table("comments"),
        id(add("id", &Comment::id, liteorm::ColumnOptions::identity())),
        post_id(add("post_id", &Comment::post_id,
                    liteorm::ColumnOptions::foreign_key("posts", "id"))),
        body(add("body", &Comment::body)) {}
};
struct NoPk { std::string field; };
struct NoPkTable : liteorm::Table<NoPk> {
    liteorm::Column<NoPk,std::string> field;
    NoPkTable() : Table("nopk"), field(add("field", &NoPk::field)) {}
};

template <typename E, typename F>
void must_throw(F f) {
    bool thrown = false;
    try { f(); } catch (const E&) { thrown = true; }
    if (!thrown) throw std::runtime_error("expected exception");
}

int main() {
    Users users;
    Posts posts;
    Comments comments;
    liteorm::Database db(":memory:");
    db.create_table(users);
    db.create_table(posts);
    db.create_table(comments);
    User u; u.name = "Alice";
    const std::int64_t alice = db.insert(users, u);
    u.name = "Bob";
    const std::int64_t bob = db.insert(users, u);
    u.name = "Nobody";
    const std::int64_t nobody = db.insert(users, u);

    Post p; p.user_id = alice; p.title = "One";
    const std::int64_t p1 = db.insert(posts, p);
    p.title = "Two";
    const std::int64_t p2 = db.insert(posts, p);
    p.user_id = bob; p.title = "Three";
    const std::int64_t p3 = db.insert(posts, p);
    Comment c; c.post_id = p1; c.body = "Hello";
    db.insert(comments, c);
    c.body = "World";
    db.insert(comments, c);
    c.post_id = p3; c.body = "OK";
    db.insert(comments, c);

    // LEFT JOIN preserves all users; a missing right row is not a default Post.
    typedef std::pair<User,liteorm::Optional<Post> > LeftRow;
    static_assert(std::is_same<
        decltype(db.left_join(users,posts).on(users.id == posts.user_id).first()),
        std::unique_ptr<LeftRow> >::value, "two-table LEFT JOIN return type");
    std::vector<LeftRow> joined = db.left_join(users, posts)
        .on(users.id == posts.user_id)
        .order_by(users.id.asc()).all();
    assert(joined.size() == 4);
    assert(joined[0].first.id == alice && joined[0].second.has_value());
    assert(joined[1].second.has_value());
    assert(joined[2].first.id == bob && joined[2].second.value().title == "Three");
    assert(joined[3].first.id == nobody && !joined[3].second.has_value());
    const std::vector<LeftRow> unmatched = db.left_join(users, posts)
        .on(users.id == posts.user_id).where(posts.id.is_null()).all();
    assert(unmatched.size() == 1 && unmatched[0].first.id == nobody);
    assert(db.left_join(users, posts).on(users.id == posts.user_id)
        .where(users.id == nobody).first()->second.has_value() == false);

    // INNER JOIN with 3 tables yields tuple<...>; a right LEFT JOIN is Optional.
    typedef std::tuple<User,Post,Comment> Triple;
    std::vector<Triple> triples = db.join(users,posts)
        .on(users.id == posts.user_id)
        .join(comments).on(posts.id == comments.post_id)
        .order_by(comments.id.asc()).all();
    assert(triples.size() == 3);
    assert(std::get<0>(triples[0]).id == alice);
    assert(std::get<1>(triples[0]).id == p1);
    assert(std::get<2>(triples[0]).body == "Hello");
    assert(std::get<2>(triples[2]).body == "OK");

    typedef std::tuple<User,Post,liteorm::Optional<Comment> > MixedRow;
    std::vector<MixedRow> mixed = db.join(users,posts)
        .on(users.id == posts.user_id)
        .left_join(comments).on(posts.id == comments.post_id)
        .order_by(posts.id.asc()).all();
    assert(mixed.size() == 4);
    assert(!std::get<2>(mixed[2]).has_value()); // post Two has no comments
    assert(std::get<1>(mixed[2]).id == p2);

    typedef std::tuple<User,liteorm::Optional<Post>,liteorm::Optional<Comment> > FullRow;
    std::vector<FullRow> all_users = db.left_join(users,posts)
        .on(users.id == posts.user_id)
        .left_join(comments).on(posts.id == comments.post_id)
        .order_by(users.id.asc()).all();
    assert(all_users.size() == 5);
    assert(std::get<0>(all_users.back()).id == nobody);
    assert(!std::get<1>(all_users.back()).has_value());
    assert(!std::get<2>(all_users.back()).has_value());

    // Typed relation metadata and batched one-to-many fetch.
    liteorm::HasMany<User,Post,std::int64_t> user_posts =
        liteorm::has_many(users, users.id, posts, posts.user_id);
    const std::vector<User> parents = db.select(users).order_by(users.id.asc()).all();
    const std::vector<std::vector<Post> > groups = db.load_many(user_posts, parents);
    assert(groups.size() == 3);
    assert(groups[0].size() == 2 && groups[1].size() == 1 && groups[2].empty());
    assert(db.load_many(user_posts, std::vector<User>()).empty());
    std::vector<User> duplicates; duplicates.push_back(parents[0]); duplicates.push_back(parents[0]);
    const std::vector<std::vector<Post> > repeated = db.load_many(user_posts, duplicates);
    assert(repeated.size() == 2 && repeated[0].size() == 2 && repeated[1].size() == 2);
    std::vector<User> many(1205);
    for (std::size_t i = 0; i < many.size(); ++i)
        many[i].id = static_cast<std::int64_t>(100000 + i);
    many[0].id = alice; many[601].id = bob; many[1204].id = alice;
    const std::vector<std::vector<Post> > batched = db.load_many(user_posts, many);
    assert(batched.size() == 1205 && batched[0].size() == 2 &&
           batched[601].size() == 1 && batched[1204].size() == 2 &&
           batched[600].empty());

    liteorm::BelongsTo<Post,User,std::int64_t> author =
        liteorm::belongs_to(posts, posts.user_id, users, users.id);
    std::unique_ptr<User> parent = db.load(author, db.select(posts).where(posts.id == p1).first().operator*());
    assert(parent && parent->id == alice && parent->name == "Alice");

    // IN is parameterized, including an empty IN list (always false).
    std::vector<std::int64_t> keys; keys.push_back(alice); keys.push_back(bob);
    const liteorm::SqlPlan sqlite_in = liteorm::select(users).where(users.id.in(keys)).compile();
    const liteorm::SqlPlan pg_in = liteorm::select(users).where(users.id.in(keys))
        .compile(liteorm::SqlDialect::PostgreSQL);
    assert(sqlite_in.sql.find(" IN (?, ?)") != std::string::npos);
    assert(pg_in.sql.find(" IN ($1, $2)") != std::string::npos);
    assert(pg_in.params.size() == 2 && pg_in.params[0].integer == alice);
    assert(db.select(users).where(users.id.in(std::vector<std::int64_t>())).all().empty());

    // Parameter order: JOIN ON comes before WHERE, then LIMIT, for both dialects.
    const liteorm::SqlPlan pg_join = liteorm::JoinChain<User,
        liteorm::JoinedPart<Post,false> >(
            users, std::make_tuple(liteorm::JoinedPart<Post,false>(posts)))
        .on((users.id == posts.user_id) && (posts.title == std::string("Two")))
        .where(users.name == std::string("Alice"))
        .limit(2).compile(liteorm::SqlDialect::PostgreSQL);
    assert(pg_join.sql.find("LEFT JOIN") == std::string::npos);
    assert(pg_join.sql.find("\"posts\".\"title\" = $1") != std::string::npos);
    assert(pg_join.sql.find("\"users\".\"name\" = $2") != std::string::npos);
    assert(pg_join.sql.find("LIMIT $3") != std::string::npos);
    assert(pg_join.params.size() == 3 && pg_join.params[0].text == "Two" &&
           pg_join.params[1].text == "Alice" && pg_join.params[2].integer == 2);

    must_throw<std::logic_error>([&]() {
        db.left_join(users, posts).all();
    });
    must_throw<std::logic_error>([&]() {
        db.join(users,posts).join(comments);
    });
    must_throw<std::invalid_argument>([&]() {
        db.join(users,posts).on(users.id == comments.post_id);
    });
    must_throw<std::invalid_argument>([&]() {
        db.join(users, posts).on(users.id == posts.user_id).join(users);
    });
    NoPkTable no_pk;
    must_throw<std::invalid_argument>([&]() {
        db.left_join(users, no_pk);
    });
    must_throw<std::invalid_argument>([&]() {
        Posts other;
        liteorm::has_many(users, users.id, posts, other.user_id);
    });

    std::cout << "SQLite LEFT/MULTI JOIN, typed relations, IN and dialect tests passed.\n";
}
