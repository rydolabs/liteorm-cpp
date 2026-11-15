#include "liteorm/liteorm.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct Author { std::int64_t id; std::string name; Author() : id(0) {} };
struct Article { std::int64_t id; std::int64_t author_id; std::string title;
    Article() : id(0), author_id(0) {} };
struct Authors : liteorm::Table<Author> {
    liteorm::Column<Author,std::int64_t> id;
    liteorm::Column<Author,std::string> name;
    Authors() : Table("authors"),
        id(add("id", &Author::id, liteorm::ColumnOptions::identity())),
        name(add("name", &Author::name)) {}
};
struct Articles : liteorm::Table<Article> {
    liteorm::Column<Article,std::int64_t> id;
    liteorm::Column<Article,std::int64_t> author_id;
    liteorm::Column<Article,std::string> title;
    Articles() : Table("articles"),
        id(add("id", &Article::id, liteorm::ColumnOptions::identity())),
        author_id(add("author_id", &Article::author_id, liteorm::ColumnOptions::foreign_key("authors", "id"))),
        title(add("title", &Article::title)) {}
};

template <typename E, typename F>
void must_throw(F f) {
    bool caught = false;
    try { f(); } catch (const E&) { caught = true; }
    if (!caught) throw std::runtime_error("expected exception");
}

int main() {
    Authors authors;
    Articles articles;
    liteorm::Database db(":memory:");
    db.create_table(authors);
    db.create_table(articles);

    Author a; a.name = "Alice";
    Author b; b.name = "Bob";
    const std::int64_t aid = db.insert(authors, a);
    const std::int64_t bid = db.insert(authors, b);
    Article first; first.author_id = aid; first.title = "First";
    Article second; second.author_id = aid; second.title = "Second";
    Article third; third.author_id = bid; third.title = "Third";
    db.insert(articles, first);
    db.insert(articles, second);
    db.insert(articles, third);
    Article orphan;
    orphan.author_id = static_cast<std::int64_t>(999999);
    orphan.title = "orphan";
    must_throw<liteorm::SqliteError>([&]() { db.insert(articles, orphan); });

    liteorm::SqlPlan pg_plan = db.join(authors, articles)
        .on(authors.id == articles.author_id)
        .where(authors.name == std::string("Alice"))
        .order_by(articles.title.asc()).limit(2).compile();
    assert(pg_plan.dialect == liteorm::SqlDialect::SQLite);
    assert(pg_plan.sql.find(" INNER JOIN \"articles\" ON \"authors\".\"id\" = \"articles\".\"author_id\"") != std::string::npos);
    assert(pg_plan.params.size() == 2);
    assert(pg_plan.params[0].text == "Alice");
    assert(pg_plan.params[1].integer == 2);

    std::vector<std::pair<Author, Article> > rows = db.join(authors, articles)
        .on(authors.id == articles.author_id)
        .where(authors.name == std::string("Alice"))
        .order_by(articles.title.asc()).all();
    assert(rows.size() == 2);
    assert(rows[0].first.id == aid && rows[0].second.title == "First");
    assert(rows[1].first.id == aid && rows[1].second.title == "Second");
    assert(db.join(authors, articles).on(authors.id == articles.author_id)
        .where(articles.title == std::string("Third")).first()->first.name == "Bob");
    assert(!db.join(authors, articles).on(authors.id == articles.author_id)
        .where(articles.title == std::string("Missing")).first());

    must_throw<std::logic_error>([&]() { db.join(authors, articles).all(); });
    must_throw<std::invalid_argument>([&]() {
        db.select(authors).where(authors.id == articles.author_id);
    });
    must_throw<std::invalid_argument>([&]() {
        db.join(authors, articles).on(authors.id == articles.author_id)
          .where(Articles().title == std::string("invalid"));
    });
    must_throw<std::invalid_argument>([&]() {
        db.join(authors, authors);
    });

    const liteorm::SqlPlan pg_sql = liteorm::JoinQuery<Author, Article>(authors, articles)
        .on(authors.id == articles.author_id)
        .where(articles.title != std::string("Ignore"))
        .limit(5).compile(liteorm::SqlDialect::PostgreSQL);
    assert(pg_sql.sql.find(" INNER JOIN ") != std::string::npos);
    assert(pg_sql.sql.find("\"title\" <> $1") != std::string::npos);
    assert(pg_sql.sql.find(" LIMIT $2") != std::string::npos);
    assert(pg_sql.params.size() == 2);

    std::cout << "SQLite INNER JOIN and PostgreSQL JOIN compilation tests passed.\n";
}
