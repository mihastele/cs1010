#ifndef TABLE_H
#define TABLE_H
#include "bplus.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Persistent single-table storage: integer primary keys with byte values.
 * Live keys are indexed by a B+ tree for ordered scans; values are cached
 * in memory, sorted by key for binary search. Every acknowledged mutation
 * is appended to the log file and synced before it becomes visible, so a
 * reopened table replays exactly the acknowledged writes. The log format
 * is shared with the Rust and C++ implementations; see table.c.
 *
 * This is an educational store, not a database: one table, no SQL,
 * transactions, concurrent-writer locking, or checksums. */
#define TABLE_MAX_VALUE (16u * 1024u * 1024u)
typedef struct {
    int32_t key;
    uint8_t *value;
    size_t length;
} table_row;
typedef struct {
    int32_t key;
    const uint8_t *value;
    size_t length;
} table_entry;
typedef struct {
    int fd;
    bplus_tree index;
    table_row *rows;
    size_t count, capacity;
    bool failed;
} table;
/* Opens or creates the table at path. Returns false on I/O, format, or
 * allocation failure. table_close is always safe after table_open. */
bool table_open(table *t, const char *path);
void table_close(table *t);
/* Inserts a new key. Returns false on I/O or allocation failure, on an
 * oversize value, or after an earlier write failed; *inserted is false
 * when the key already exists. A failed insert may still mark the table
 * failed when the record reached the log; reopen it then. */
bool table_insert(table *t, int32_t key, const uint8_t *value, size_t length, bool *inserted);
/* Returns the borrowed value for key, or NULL when absent. length_out may
 * be NULL. The pointer stays valid until the next mutation or close. */
const uint8_t *table_get(const table *t, int32_t key, size_t *length_out);
/* Deletes a key. Returns false on I/O failure or after an earlier write
 * failed; *deleted is false when the key was absent. */
bool table_delete(table *t, int32_t key, bool *deleted);
/* Inclusive range in ascending key order. Returns the total number of
 * matches and writes at most capacity borrowed entries; pass NULL with
 * capacity zero to count. Returns 0 on allocation failure. */
size_t table_range(const table *t, int32_t first, int32_t last, table_entry *out, size_t capacity);
#endif
