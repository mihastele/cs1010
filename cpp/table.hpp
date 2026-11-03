#pragma once
#include "bplus.hpp"
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <io.h>
#define DB_SYNC _commit
#define DB_BINARY O_BINARY
#else
#include <unistd.h>
#define DB_SYNC fsync
#define DB_BINARY 0
#endif
#include <unordered_map>
#include <utility>
#include <vector>

// Persistent single-table storage: integer primary keys with byte values.
// Live keys are indexed by a B+ tree for ordered scans; values are cached
// in memory. Every acknowledged mutation is appended to the log file and
// synced before it becomes visible, so a reopened table replays exactly
// the acknowledged writes.
//
// Log format, shared with the C and Rust implementations: an 8-byte
// header followed by append-only records. Integers are little-endian so
// files stay comparable across platforms.
//   header: "CSDB" 0x01 0x00 0x00 0x00
//   insert: 0x01 key[i32] length[u32] value[length bytes]
//   delete: 0x02 key[i32]
//
// This is an educational store, not a database: one table, no SQL,
// transactions, concurrent-writer locking, or checksums. A torn tail
// record fails to open rather than silently losing acknowledged data.
class Table {
  public:
    struct Row {
        std::int32_t key;
        std::vector<std::uint8_t> value;
    };
    static constexpr std::size_t kMaxValue = 16u * 1024u * 1024u;

    // Opens or creates the table at path. Throws on I/O, format, or
    // allocation failure.
    explicit Table(const std::string &path) : index_(8) {
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT | DB_BINARY, 0666);
        if (fd < 0)
            throw std::runtime_error("open " + path + ": " + std::strerror(errno));
        try {
            off_t size = ::lseek(fd, 0, SEEK_END);
            if (size < 0)
                throw std::runtime_error("stat " + path + ": " + std::strerror(errno));
            if (size == 0) {
                write_all(fd, kMagic, sizeof kMagic);
                if (::DB_SYNC(fd) < 0)
                    throw std::runtime_error("sync " + path + ": " + std::strerror(errno));
            } else {
                if (::lseek(fd, 0, SEEK_SET) < 0)
                    throw std::runtime_error("seek " + path + ": " + std::strerror(errno));
                std::uint8_t header[8];
                read_exact(fd, header, sizeof header);
                if (std::memcmp(header, kMagic, sizeof kMagic) != 0)
                    throw std::runtime_error(path + ": invalid database header");
                replay(fd, path);
            }
        } catch (...) {
            ::close(fd);
            throw;
        }
        fd_ = fd;
    }
    ~Table() {
        if (fd_ >= 0)
            ::close(fd_);
    }
    Table(const Table &) = delete;
    Table &operator=(const Table &) = delete;

    // Inserts a new key. Returns false when the key already exists. Throws
    // on I/O or allocation failure, on an oversize value, or after an
    // earlier write failed; reopen the table then.
    bool insert(std::int32_t key, const std::vector<std::uint8_t> &value) {
        fail_if_broken();
        if (value.size() > kMaxValue)
            throw std::invalid_argument("value exceeds size limit");
        if (index_.contains(key))
            return false;
        std::vector<std::uint8_t> record;
        record.reserve(9 + value.size());
        record.push_back(1);
        append_u32(record, static_cast<std::uint32_t>(key));
        append_u32(record, static_cast<std::uint32_t>(value.size()));
        record.insert(record.end(), value.begin(), value.end());
        append(record);
        try {
            index_.insert(key);
            values_.emplace(key, value);
        } catch (...) {
            // The record is already logged; reopening replays it. Until
            // then memory disagrees with the log, so refuse further use.
            failed_ = true;
            throw;
        }
        return true;
    }
    // Returns the borrowed value for key, or nullptr when absent. The
    // pointer stays valid until the next mutation or destruction.
    const std::vector<std::uint8_t> *get(std::int32_t key) const {
        auto it = values_.find(key);
        return it == values_.end() ? nullptr : &it->second;
    }
    // Deletes a key. Returns false when the key was absent. Throws on I/O
    // failure or after an earlier write failed.
    bool erase(std::int32_t key) {
        fail_if_broken();
        if (!index_.contains(key))
            return false;
        std::vector<std::uint8_t> record;
        record.push_back(2);
        append_u32(record, static_cast<std::uint32_t>(key));
        append(record);
        index_.remove(key);
        values_.erase(key);
        return true;
    }
    // Rows in ascending key order, including both bounds.
    std::vector<Row> range(std::int32_t first, std::int32_t last) const {
        std::vector<Row> rows;
        for (int key : index_.range(first, last)) {
            auto it = values_.find(key);
            // The index and the value cache always agree; see insert/erase.
            auto row_key = static_cast<std::int32_t>(key);
            rows.push_back(Row{row_key, it->second});
        }
        return rows;
    }

  private:
    static constexpr std::uint8_t kMagic[8] = {'C', 'S', 'D', 'B', 1, 0, 0, 0};
    int fd_ = -1;
    BPlusTree index_;
    std::unordered_map<std::int32_t, std::vector<std::uint8_t>> values_;
    bool failed_ = false;

    void fail_if_broken() const {
        if (failed_)
            throw std::runtime_error("table cannot be used after a write failure; reopen it");
    }
    static void read_exact(int fd, std::uint8_t *buf, std::size_t n) {
        while (n > 0) {
            ssize_t r = ::read(fd, buf, n);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                throw std::runtime_error(std::string("read: ") + std::strerror(errno));
            }
            if (r == 0)
                throw std::runtime_error("truncated record");
            buf += r;
            n -= static_cast<std::size_t>(r);
        }
    }
    static void write_all(int fd, const std::uint8_t *buf, std::size_t n) {
        while (n > 0) {
            ssize_t r = ::write(fd, buf, n);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                throw std::runtime_error(std::string("write: ") + std::strerror(errno));
            }
            buf += r;
            n -= static_cast<std::size_t>(r);
        }
    }
    static std::uint32_t load_u32(const std::uint8_t *p) {
        return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
               static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
    }
    static void append_u32(std::vector<std::uint8_t> &out, std::uint32_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
        out.push_back(static_cast<std::uint8_t>(v >> 16));
        out.push_back(static_cast<std::uint8_t>(v >> 24));
    }
    void append(const std::vector<std::uint8_t> &record) {
        try {
            write_all(fd_, record.data(), record.size());
            if (::DB_SYNC(fd_) < 0)
                throw std::runtime_error(std::string("sync: ") + std::strerror(errno));
        } catch (...) {
            failed_ = true;
            throw;
        }
    }
    // Replay is lenient about duplicates, matching the C and Rust
    // implementations: the last insert wins, and deleting an absent key
    // is a no-op.
    void replay(int fd, const std::string &path) {
        for (;;) {
            std::uint8_t kind;
            ssize_t r;
            do {
                r = ::read(fd, &kind, 1);
            } while (r < 0 && errno == EINTR);
            if (r < 0)
                throw std::runtime_error("read " + path + ": " + std::strerror(errno));
            if (r == 0)
                return;
            std::uint8_t raw[4];
            read_exact(fd, raw, sizeof raw);
            auto key = static_cast<std::int32_t>(load_u32(raw));
            if (kind == 1) {
                read_exact(fd, raw, sizeof raw);
                std::size_t length = load_u32(raw);
                if (length > kMaxValue)
                    throw std::runtime_error(path + ": value exceeds size limit");
                std::vector<std::uint8_t> value(length);
                if (length > 0)
                    read_exact(fd, value.data(), length);
                index_.insert(key);
                values_[key] = std::move(value);
            } else if (kind == 2) {
                index_.remove(key);
                values_.erase(key);
            } else {
                throw std::runtime_error(path + ": invalid record type");
            }
        }
    }
};
