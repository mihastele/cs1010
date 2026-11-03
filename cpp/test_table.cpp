#include "table.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>
static void require(bool ok) {
    if (!ok)
        std::abort();
}
template <typename F> static void require_throws(F &&f) {
    try {
        f();
    } catch (...) {
        return;
    }
    std::abort();
}
static std::vector<std::uint8_t> to_bytes(const std::string &s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}
static std::string temp_path() {
    static unsigned next = 0;
    auto path = std::filesystem::temp_directory_path() /
                ("cs1010-table-" + std::to_string(::getpid()) + "-" + std::to_string(next++));
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path.string();
}
// Inserts "row<k>" for 99..0, replaces 5, and deletes 6; mirrors the C
// and Rust persistence tests.
static void test_persistence() {
    std::string path = temp_path();
    {
        Table t(path);
        for (int k = 99; k >= 0; --k)
            require(t.insert(k, to_bytes("row" + std::to_string(k))));
        require(!t.insert(5, to_bytes("duplicate")));
        require(t.erase(5));
        require(!t.erase(5));
        require(t.insert(5, to_bytes("replacement")));
        require(t.erase(6));
        require(t.insert(1000, {})); // Empty values round-trip.
        require(t.insert(1001, {0x0a, 0x0d, 0x1a, 0x00}));
    }
    {
        Table t(path);
        auto v = t.get(5);
        require(v && *v == to_bytes("replacement"));
        require(t.get(6) == nullptr);
        v = t.get(1000);
        require(v && v->empty());
        v = t.get(1001);
        require(v && *v == std::vector<std::uint8_t>({0x0a, 0x0d, 0x1a, 0x00}));
        auto rows = t.range(3, 8);
        require(rows.size() == 5);
        std::int32_t want[] = {3, 4, 5, 7, 8};
        for (std::size_t i = 0; i < rows.size(); ++i) {
            require(rows[i].key == want[i]);
            require(rows[i].value ==
                    (want[i] == 5 ? to_bytes("replacement")
                                  : to_bytes("row" + std::to_string(want[i]))));
        }
        require(t.range(8, 3).empty());
    }
    std::filesystem::remove(path);
}
static void write_file(const std::string &path, const std::vector<std::uint8_t> &data) {
    FILE *f = std::fopen(path.c_str(), "wb");
    require(f);
    require(std::fwrite(data.data(), 1, data.size(), f) == data.size());
    require(std::fclose(f) == 0);
}
static void test_invalid_files() {
    std::string path = temp_path();
    write_file(path, to_bytes("bad header"));
    require_throws([&] { Table t(path); });
    write_file(path, {'C', 'S', 'D', 'B', 1, 0, 0, 0, 1, 0, 0});
    require_throws([&] { Table t(path); });
    write_file(path, {'C', 'S', 'D', 'B', 1, 0, 0, 0, 9, 0, 0, 0, 0});
    require_throws([&] { Table t(path); });
    // Declared length over the limit is rejected without allocating it.
    write_file(path, {'C', 'S', 'D', 'B', 1, 0, 0, 0, 1, 7, 0, 0, 0, 1, 0, 0, 1});
    require_throws([&] { Table t(path); });
    // Truncated value bytes are rejected.
    write_file(path, {'C', 'S', 'D', 'B', 1, 0, 0, 0, 1, 7, 0, 0, 0, 3, 0, 0, 0, 'a', 'b'});
    require_throws([&] { Table t(path); });
    // Writes that cannot persist fail cleanly.
#ifndef _WIN32
    require_throws([] { Table t("/dev/full"); });
#endif
    std::filesystem::remove(path);
    {
    Table t(path);
    bool invalid = false;
    try {
        t.insert(1, std::vector<std::uint8_t>(Table::kMaxValue + 1));
    } catch (const std::invalid_argument &) {
        invalid = true;
    }
    require(invalid);
    require(t.get(1) == nullptr);
    }
    std::filesystem::remove(path);
}
int main() {
    test_persistence();
    test_invalid_files();
}
