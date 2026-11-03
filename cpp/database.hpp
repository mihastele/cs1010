#pragma once
#include "table.hpp"
#include <cctype>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// A deliberately small SQL interface; each table has exactly an integer
// primary key named id and a text column named value.
class Database {
  public:
    struct Result {
        enum Kind { Created, Inserted, Deleted, Rows } kind;
        std::size_t affected = 0;
        std::vector<std::pair<std::int32_t, std::string>> rows;
    };

  private:
    enum class Type { Word, Text, Number, Symbol };
    struct Token {
        Type type;
        std::string text;
    };
    struct Parser {
        std::vector<Token> tokens;
        std::size_t pos = 0;
        static bool alpha(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }
        static bool digit(char c) {
            return c >= '0' && c <= '9';
        }
        explicit Parser(const std::string &sql) {
            for (std::size_t i = 0; i < sql.size();) {
                unsigned char c = static_cast<unsigned char>(sql[i]);
                if (std::isspace(c)) {
                    ++i;
                    continue;
                }
                if (std::string("(),=;*").find(c) != std::string::npos) {
                    tokens.push_back({Type::Symbol, std::string(1, c)});
                    ++i;
                    continue;
                }
                if (c == '\'') {
                    ++i;
                    std::string text;
                    bool closed = false;
                    while (i < sql.size()) {
                        if (sql[i] == '\'') {
                            if (i + 1 < sql.size() && sql[i + 1] == '\'') {
                                text.push_back('\'');
                                i += 2;
                            } else {
                                ++i;
                                closed = true;
                                break;
                            }
                        } else
                            text.push_back(sql[i++]);
                    }
                    if (!closed)
                        throw std::invalid_argument("unterminated string");
                    tokens.push_back({Type::Text, std::move(text)});
                    continue;
                }
                if (alpha(c) || c == '_') {
                    std::string word;
                    while (i < sql.size() && (alpha(sql[i]) || digit(sql[i]) || sql[i] == '_'))
                        word.push_back(
                            static_cast<char>(std::tolower(static_cast<unsigned char>(sql[i++]))));
                    tokens.push_back({Type::Word, std::move(word)});
                    continue;
                }
                if (digit(c) || c == '-') {
                    std::size_t start = i++;
                    while (i < sql.size() && digit(sql[i]))
                        ++i;
                    tokens.push_back({Type::Number, sql.substr(start, i - start)});
                    continue;
                }
                throw std::invalid_argument("unexpected character");
            }
            if (!tokens.empty() && tokens.back().type == Type::Symbol && tokens.back().text == ";")
                tokens.pop_back();
        }
        Token take() {
            if (pos == tokens.size())
                throw std::invalid_argument("unexpected end of statement");
            return tokens[pos++];
        }
        void expect(Type type, const std::string &value) {
            auto t = take();
            if (t.type != type || t.text != value)
                throw std::invalid_argument("expected " + value);
        }
        void word(const std::string &w) {
            expect(Type::Word, w);
        }
        void symbol(char c) {
            expect(Type::Symbol, std::string(1, c));
        }
        std::string name() {
            auto t = take();
            if (t.type != Type::Word || t.text.size() > 64)
                throw std::invalid_argument("invalid table name");
            return t.text;
        }
        std::int32_t number() {
            auto t = take();
            if (t.type != Type::Number)
                throw std::invalid_argument("expected integer");
            try {
                std::size_t n = 0;
                long long x = std::stoll(t.text, &n);
                if (n != t.text.size() || x < INT32_MIN || x > INT32_MAX)
                    throw std::invalid_argument("range");
                return static_cast<std::int32_t>(x);
            } catch (const std::exception &) {
                throw std::invalid_argument("invalid integer");
            }
        }
        std::string text() {
            auto t = take();
            if (t.type != Type::Text)
                throw std::invalid_argument("expected quoted text");
            return t.text;
        }
        void done() {
            if (pos != tokens.size())
                throw std::invalid_argument("unexpected trailing tokens");
        }
    };
    std::filesystem::path directory_;
    std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
    std::filesystem::path path(const std::string &name) const {
        return directory_ / ("table_" + name + ".csdb");
    }
    Table &table(const std::string &name) {
        auto i = tables_.find(name);
        if (i == tables_.end()) {
            auto file = path(name);
            if (!std::filesystem::is_regular_file(file))
                throw std::runtime_error("table does not exist");
            i = tables_.emplace(name, std::make_unique<Table>(file.string())).first;
        }
        return *i->second;
    }

  public:
    explicit Database(const std::filesystem::path &directory) : directory_(directory) {
        std::filesystem::create_directories(directory_);
    }
    Result execute(const std::string &sql) {
        Parser p(sql);
        auto command = p.take();
        if (command.type != Type::Word)
            throw std::invalid_argument("expected statement");
        if (command.text == "create") {
            p.word("table");
            auto name = p.name();
            p.symbol('(');
            p.word("id");
            p.word("integer");
            p.word("primary");
            p.word("key");
            p.symbol(',');
            p.word("value");
            p.word("text");
            p.symbol(')');
            p.done();
            auto file = path(name);
            int fd = ::open(file.string().c_str(), O_WRONLY | O_CREAT | O_EXCL | DB_BINARY, 0666);
            if (fd < 0)
                throw std::runtime_error("cannot create table");
            ::close(fd);
            tables_.emplace(name, std::make_unique<Table>(file.string()));
            return {Result::Created};
        }
        if (command.text == "insert") {
            p.word("into");
            auto name = p.name();
            p.word("values");
            p.symbol('(');
            auto id = p.number();
            p.symbol(',');
            auto value = p.text();
            p.symbol(')');
            p.done();
            if (!table(name).insert(id, std::vector<std::uint8_t>(value.begin(), value.end())))
                throw std::runtime_error("duplicate primary key");
            return {Result::Inserted};
        }
        if (command.text == "select") {
            p.symbol('*');
            p.word("from");
            auto name = p.name();
            auto lo = INT32_MIN, hi = INT32_MAX;
            if (p.pos < p.tokens.size()) {
                p.word("where");
                p.word("id");
                p.symbol('=');
                lo = hi = p.number();
            }
            p.done();
            Result result{Result::Rows};
            for (auto &row : table(name).range(lo, hi))
                result.rows.emplace_back(row.key, std::string(row.value.begin(), row.value.end()));
            return result;
        }
        if (command.text == "delete") {
            p.word("from");
            auto name = p.name();
            p.word("where");
            p.word("id");
            p.symbol('=');
            auto id = p.number();
            p.done();
            return {Result::Deleted, table(name).erase(id) ? 1u : 0u};
        }
        throw std::invalid_argument("unsupported SQL statement");
    }
};
