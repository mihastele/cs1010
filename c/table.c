#define _POSIX_C_SOURCE 200809L
#include "table.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/* Log format, shared with the Rust and C++ implementations: an 8-byte
 * header followed by append-only records. Integers are little-endian so
 * files stay comparable across platforms.
 *   header: "CSDB" 0x01 0x00 0x00 0x00
 *   insert: 0x01 key[i32] length[u32] value[length bytes]
 *   delete: 0x02 key[i32]
 * Each mutation is written and synced before it becomes visible, so a
 * reopened table replays exactly the acknowledged writes. A torn tail
 * record fails to open rather than silently losing acknowledged data. */
static const uint8_t magic[8] = {'C', 'S', 'D', 'B', 1, 0, 0, 0};
/* Keys cross the int32_t/int boundary into the B+ tree; the casts below
 * round-trip exactly when int holds 32 bits. */
typedef char int_wide_enough[sizeof(int) >= 4 ? 1 : -1];
static int32_t load_key(const uint8_t *p) {
    uint32_t u = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
                 (uint32_t)p[3] << 24;
    int32_t key;
    memcpy(&key, &u, sizeof key);
    return key;
}
static uint32_t load_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void store_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static bool read_exact(int fd, uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t r = read(fd, buf, n);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (r == 0)
            return false;
        buf += r;
        n -= (size_t)r;
    }
    return true;
}
static bool write_all(int fd, const uint8_t *buf, size_t n) {
    while (n > 0) {
        ssize_t r = write(fd, buf, n);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        buf += r;
        n -= (size_t)r;
    }
    return true;
}
/* Returns the position of key, or where it would be inserted. */
static size_t search(const table *t, int32_t key, bool *found) {
    size_t lo = 0, hi = t->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (t->rows[mid].key < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    *found = lo < t->count && t->rows[lo].key == key;
    return lo;
}
/* Inserts or replaces a row. Takes ownership of value on success; the
 * caller frees it on failure. */
static bool rows_upsert(table *t, int32_t key, uint8_t *value, size_t length) {
    bool found;
    size_t at = search(t, key, &found);
    if (found) {
        free(t->rows[at].value);
        t->rows[at].value = value;
        t->rows[at].length = length;
        return true;
    }
    if (t->count == t->capacity) {
        size_t grown = t->capacity ? 2 * t->capacity : 16;
        table_row *rows = realloc(t->rows, grown * sizeof *rows);
        if (!rows)
            return false;
        t->rows = rows;
        t->capacity = grown;
    }
    memmove(&t->rows[at + 1], &t->rows[at], (t->count - at) * sizeof *t->rows);
    t->rows[at].key = key;
    t->rows[at].value = value;
    t->rows[at].length = length;
    t->count++;
    return true;
}
static void rows_remove(table *t, int32_t key) {
    bool found;
    size_t at = search(t, key, &found);
    if (!found)
        return;
    free(t->rows[at].value);
    memmove(&t->rows[at], &t->rows[at + 1], (t->count - at - 1) * sizeof *t->rows);
    t->count--;
}
static bool append(table *t, const uint8_t *record, size_t n) {
    if (t->failed)
        return false;
    if (!write_all(t->fd, record, n) || fsync(t->fd) < 0) {
        t->failed = true;
        return false;
    }
    return true;
}
bool table_open(table *t, const char *path) {
    t->fd = -1;
    t->rows = NULL;
    t->count = t->capacity = 0;
    t->failed = false;
    if (!bplus_init(&t->index, 8))
        return false;
    t->fd = open(path, O_RDWR | O_CREAT, 0666);
    if (t->fd < 0)
        goto fail;
    struct stat st;
    if (fstat(t->fd, &st) < 0)
        goto fail;
    if (st.st_size == 0) {
        if (!write_all(t->fd, magic, sizeof magic) || fsync(t->fd) < 0)
            goto fail;
    } else {
        uint8_t header[8];
        if (!read_exact(t->fd, header, sizeof header) || memcmp(header, magic, sizeof magic) != 0)
            goto fail;
    }
    for (;;) {
        uint8_t kind, raw[4];
        ssize_t r;
        do {
            r = read(t->fd, &kind, 1);
        } while (r < 0 && errno == EINTR);
        if (r < 0)
            goto fail;
        if (r == 0)
            return true;
        if (!read_exact(t->fd, raw, 4))
            goto fail;
        int32_t key = load_key(raw);
        if (kind == 1) {
            uint8_t lraw[4];
            if (!read_exact(t->fd, lraw, 4))
                goto fail;
            size_t length = load_u32(lraw);
            if (length > TABLE_MAX_VALUE)
                goto fail;
            /* Replay is lenient about duplicates, matching the Rust
             * implementation: the last insert wins, and deleting an
             * absent key is a no-op. */
            uint8_t *value = malloc(length ? length : 1);
            if (!value)
                goto fail;
            if (!read_exact(t->fd, value, length)) {
                free(value);
                goto fail;
            }
            bool inserted;
            if (!bplus_insert(&t->index, (int)key, &inserted)) {
                free(value);
                goto fail;
            }
            if (!rows_upsert(t, key, value, length)) {
                free(value);
                goto fail;
            }
        } else if (kind == 2) {
            bplus_remove(&t->index, (int)key);
            rows_remove(t, key);
        } else {
            goto fail;
        }
    }
fail:
    table_close(t);
    return false;
}
void table_close(table *t) {
    if (t->fd >= 0)
        close(t->fd);
    t->fd = -1;
    for (size_t i = 0; i < t->count; i++)
        free(t->rows[i].value);
    free(t->rows);
    t->rows = NULL;
    t->count = t->capacity = 0;
    bplus_destroy(&t->index);
}
bool table_insert(table *t, int32_t key, const uint8_t *value, size_t length, bool *inserted) {
    *inserted = false;
    if (t->failed || length > TABLE_MAX_VALUE)
        return false;
    if (bplus_contains(&t->index, (int)key))
        return true;
    /* One buffer keeps the record a single write, like the Rust log. */
    uint8_t *record = malloc(9 + length), *copy = malloc(length ? length : 1);
    if (!record || !copy) {
        free(record);
        free(copy);
        return false;
    }
    record[0] = 1;
    uint32_t bits;
    memcpy(&bits, &key, sizeof bits);
    store_u32(record + 1, bits);
    store_u32(record + 5, (uint32_t)length);
    if (length > 0) {
        memcpy(record + 9, value, length);
        memcpy(copy, value, length);
    }
    if (!append(t, record, 9 + length)) {
        free(record);
        free(copy);
        return false;
    }
    free(record);
    bool fresh;
    if (!bplus_insert(&t->index, (int)key, &fresh) || !rows_upsert(t, key, copy, length)) {
        /* The record is already logged; reopening replays it. Until then
         * memory disagrees with the log, so refuse further use. */
        t->failed = true;
        free(copy);
        return false;
    }
    *inserted = true;
    return true;
}
const uint8_t *table_get(const table *t, int32_t key, size_t *length_out) {
    bool found;
    size_t at = search(t, key, &found);
    if (!found)
        return NULL;
    if (length_out)
        *length_out = t->rows[at].length;
    return t->rows[at].value;
}
bool table_delete(table *t, int32_t key, bool *deleted) {
    *deleted = false;
    if (t->failed)
        return false;
    if (!bplus_contains(&t->index, (int)key))
        return true;
    uint8_t record[5];
    record[0] = 2;
    uint32_t bits;
    memcpy(&bits, &key, sizeof bits);
    store_u32(record + 1, bits);
    if (!append(t, record, sizeof record))
        return false;
    bplus_remove(&t->index, (int)key);
    rows_remove(t, key);
    *deleted = true;
    return true;
}
size_t table_range(const table *t, int32_t first, int32_t last, table_entry *out, size_t capacity) {
    size_t total = bplus_range(&t->index, (int)first, (int)last, NULL, 0);
    if (total == 0 || capacity == 0)
        return total;
    int *keys = malloc(total * sizeof *keys);
    if (!keys)
        return 0;
    bplus_range(&t->index, (int)first, (int)last, keys, total);
    size_t n = total < capacity ? total : capacity;
    for (size_t i = 0; i < n; i++) {
        /* The index and the row cache always agree; see insert/delete. */
        bool found;
        size_t at = search(t, (int32_t)keys[i], &found);
        (void)found;
        out[i].key = (int32_t)keys[i];
        out[i].value = t->rows[at].value;
        out[i].length = t->rows[at].length;
    }
    free(keys);
    return total;
}
