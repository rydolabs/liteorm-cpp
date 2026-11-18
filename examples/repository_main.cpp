#include "liteorm/liteorm.hpp"
#include <cstdint>
#include <string>
#include <iostream>
struct User {
    std::int64_t id;std::string name;int age;
    User():id(0),age(0){}
};
int main(){
    auto db = liteorm::Database::sqlite(":memory:");
    db.model<User>("users").primary_key("id",&User::id)
        .field("name",&User::name).field("age",&User::age);
    auto users=db.repository<User>();users.create_table();
    User alice;alice.name="Alice";alice.age=27;users.create(alice);
    auto found=users.find(alice.id);
    auto adults=users.where(liteorm::col(&User::age)>=18)
        .order_by(&User::name).limit(10).all();
    alice.age=28;users.update(alice);users.remove(alice.id);
    std::cout<<(found.has_value()?found.value().name:"missing")<<"\n";
    return adults.size()==1&&users.count()==0?0:1;
}
