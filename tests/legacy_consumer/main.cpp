#include <tinyorm/tinyorm.hpp>
#include <string>
struct LegacyUser { int id; std::string name; LegacyUser():id(0){} };
struct LegacyUsers: tinyorm::Table<LegacyUser> {
    tinyorm::Column<LegacyUser,int> id;
    tinyorm::Column<LegacyUser,std::string> name;
    LegacyUsers(): Table("legacy_users"),id(add("id", &LegacyUser::id, tinyorm::ColumnOptions::identity())),name(add("name", &LegacyUser::name)){}
};
int main(){
  tinyorm::Database db(":memory:");
  LegacyUsers users;
  db.create_table(users);
  LegacyUser u; u.name="legacy";
  db.insert(users,u);
  return db.select(users).count()==1 ? 0:1;
}
