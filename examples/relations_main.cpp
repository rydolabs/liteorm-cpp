#include "liteorm/liteorm.hpp"
#include <cstdint>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

struct User {
    std::int64_t id;
    std::string name;
    User() : id(0) {}
};
struct Post {
    std::int64_t id, user_id;
    std::string title;
    Post() : id(0), user_id(0) {}
};
struct Users : liteorm::Table<User> {
    liteorm::Column<User,std::int64_t> id;
    liteorm::Column<User,std::string> name;
    Users() : Table("users"),
        id(add("id", &User::id, liteorm::ColumnOptions::identity())),
        name(add("name", &User::name)) {}
};
struct Posts : liteorm::Table<Post> {
    liteorm::Column<Post,std::int64_t> id;
    liteorm::Column<Post,std::int64_t> user_id;
    liteorm::Column<Post,std::string> title;
    Posts() : Table("posts"),
        id(add("id", &Post::id, liteorm::ColumnOptions::identity())),
        user_id(add("user_id", &Post::user_id,
                    liteorm::ColumnOptions::foreign_key("users", "id"))),
        title(add("title", &Post::title)) {}
};

int main() {
    liteorm::Database db(":memory:");
    Users users;
    Posts posts;
    db.create_table(users);
    db.create_table(posts);

    User alice; alice.name = "Alice";
    User bob; bob.name = "Bob";
    const std::int64_t id = db.insert(users, alice);
    db.insert(users, bob);
    Post one; one.user_id = id; one.title = "Hello C++11";
    db.insert(posts, one);

    typedef std::pair<User,liteorm::Optional<Post> > Row;
    const std::vector<Row> rows = db.left_join(users, posts)
        .on(users.id == posts.user_id).order_by(users.id.asc()).all();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        std::cout << rows[i].first.name << " -> "
                  << (rows[i].second.has_value() ? rows[i].second.value().title : "(no post)")
                  << "\n";
    }

    const liteorm::HasMany<User,Post,std::int64_t> relation =
        liteorm::has_many(users, users.id, posts, posts.user_id);
    const std::vector<User> all = db.select(users).order_by(users.id.asc()).all();
    const std::vector<std::vector<Post> > grouped = db.load_many(relation, all);
    for (std::size_t i = 0; i < grouped.size(); ++i)
        std::cout << all[i].name << " has " << grouped[i].size() << " posts\n";
}
