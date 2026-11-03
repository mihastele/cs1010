#include "database.hpp"
#include <chrono>
#include <cstdlib>
static void require(bool b) {
    if (!b)
        std::abort();
}
template <class F> void expect_error(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    std::abort();
}
int main() {
    auto dir = std::filesystem::temp_directory_path() /
               ("cs1010-sql-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        Database db(dir);
        require(db.execute("CREATE TABLE People (id INTEGER PRIMARY KEY, value TEXT);").kind ==
                Database::Result::Created);
        db.execute("CREATE TABLE other (id INTEGER PRIMARY KEY, value TEXT)");
        db.execute("INSERT INTO people VALUES (2, 'O''Brien')");
        db.execute("INSERT INTO people VALUES (-1, 'hello')");
        db.execute("INSERT INTO other VALUES (2, 'other')");
        expect_error([&] { db.execute("insert into people values (2, 'duplicate')"); });
        expect_error([&] { db.execute("insert into people values (3, 'bad)"); });
        expect_error([&] { db.execute("delete from people"); });
    }
    {
        Database db(dir);
        auto rows = db.execute("select * from PEOPLE").rows;
        require(rows.size() == 2 && rows[0].first == -1 && rows[1].second == "O'Brien");
        require(db.execute("select * from other where id = 2").rows[0].second == "other");
        require(db.execute("delete from people where id = 2").affected == 1);
        require(db.execute("delete from people where id = 2").affected == 0);
        expect_error(
            [&] { db.execute("create table people (id integer primary key, value text)"); });
    }
    {
        Database db(dir);
        require(db.execute("select * from people where id = 2").rows.empty());
    }
    std::filesystem::remove_all(dir);
}
