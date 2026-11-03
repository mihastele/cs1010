#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static long remaining = -1;
static void *test_malloc(size_t n) {
    if (remaining == 0)
        return NULL;
    if (remaining > 0)
        --remaining;
    return malloc(n);
}
static void *test_realloc(void *p, size_t n) {
    if (remaining == 0)
        return NULL;
    if (remaining > 0)
        --remaining;
    return realloc(p, n);
}
#define malloc test_malloc
#define realloc test_realloc
#include "table.c"
#undef malloc
#undef realloc
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "failed: %s at line %d\n", #x, __LINE__);                              \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
static void make_temp(char *out) {
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir)
        dir = "/tmp";
    CHECK(snprintf(out, 256, "%s/cs1010-table-XXXXXX", dir) < 256);
    int fd = mkstemp(out);
    CHECK(fd >= 0);
    close(fd);
}
/* Inserts "row<k>" for 99..0, replaces 5, and deletes 6; mirrors the Rust
 * persistence test. */
static void test_persistence(void) {
    char path[256];
    make_temp(path);
    {
        table t;
        CHECK(table_open(&t, path));
        for (int k = 99; k >= 0; k--) {
            char value[16];
            int len = snprintf(value, sizeof value, "row%d", k);
            bool inserted;
            CHECK(table_insert(&t, k, (uint8_t *)value, (size_t)len, &inserted) && inserted);
        }
        bool done;
        CHECK(table_insert(&t, 5, (uint8_t *)"duplicate", 9, &done) && !done);
        CHECK(table_delete(&t, 5, &done) && done);
        CHECK(table_delete(&t, 5, &done) && !done);
        CHECK(table_insert(&t, 5, (uint8_t *)"replacement", 11, &done) && done);
        CHECK(table_delete(&t, 6, &done) && done);
        /* Empty values round-trip. */
        CHECK(table_insert(&t, 1000, NULL, 0, &done) && done);
        table_close(&t);
    }
    {
        table t;
        CHECK(table_open(&t, path));
        size_t len = 0;
        const uint8_t *v = table_get(&t, 5, &len);
        CHECK(v && len == 11 && memcmp(v, "replacement", 11) == 0);
        CHECK(table_get(&t, 6, NULL) == NULL);
        CHECK(table_get(&t, 1000, &len) != NULL && len == 0);
        table_entry rows[8];
        CHECK(table_range(&t, 3, 8, rows, 8) == 5);
        int32_t want[] = {3, 4, 5, 7, 8};
        for (int i = 0; i < 5; i++) {
            char expect[16];
            int n = snprintf(expect, sizeof expect, i == 2 ? "replacement" : "row%d", want[i]);
            CHECK(rows[i].key == want[i]);
            CHECK(rows[i].length == (size_t)n && memcmp(rows[i].value, expect, (size_t)n) == 0);
        }
        CHECK(table_range(&t, 8, 3, NULL, 0) == 0);
        /* Capacity truncates the output but still reports the total. */
        CHECK(table_range(&t, 3, 8, rows, 2) == 5);
        CHECK(rows[0].key == 3 && rows[1].key == 4);
        CHECK(table_range(&t, 3, 8, NULL, 0) == 5);
        table_close(&t);
    }
    CHECK(remove(path) == 0);
}
static void write_file(const char *path, const uint8_t *data, size_t n) {
    FILE *f = fopen(path, "wb");
    CHECK(f);
    CHECK(fwrite(data, 1, n, f) == n);
    CHECK(fclose(f) == 0);
}
static void test_invalid_files(void) {
    char path[256];
    make_temp(path);
    table t;
    write_file(path, (uint8_t *)"bad header", 10);
    CHECK(!table_open(&t, path));
    uint8_t truncated[] = {'C', 'S', 'D', 'B', 1, 0, 0, 0, 1, 0, 0};
    write_file(path, truncated, sizeof truncated);
    CHECK(!table_open(&t, path));
    uint8_t bad_kind[] = {'C', 'S', 'D', 'B', 1, 0, 0, 0, 9, 0, 0, 0, 0};
    write_file(path, bad_kind, sizeof bad_kind);
    CHECK(!table_open(&t, path));
    /* Declared length over the limit is rejected without allocating it. */
    uint8_t huge[] = {'C', 'S', 'D', 'B', 1, 0, 0, 0, 1, 7, 0, 0, 0, 1, 0, 0, 1};
    write_file(path, huge, sizeof huge);
    CHECK(!table_open(&t, path));
    /* Truncated value bytes are rejected. */
    uint8_t torn[] = {'C', 'S', 'D', 'B', 1, 0, 0, 0, 1, 7, 0, 0, 0, 3, 0, 0, 0, 'a', 'b'};
    write_file(path, torn, sizeof torn);
    CHECK(!table_open(&t, path));
    /* Writes that cannot persist fail cleanly. */
    CHECK(!table_open(&t, "/dev/full"));
    CHECK(remove(path) == 0);
    CHECK(table_open(&t, path));
    uint8_t *big = malloc(TABLE_MAX_VALUE + 1);
    CHECK(big);
    bool done = true;
    CHECK(!table_insert(&t, 1, big, TABLE_MAX_VALUE + 1, &done) && !done);
    table_close(&t);
    free(big);
    CHECK(remove(path) == 0);
}
/* Allocation failure during replay either fails the open or yields a
 * complete table; it never yields a corrupt one. */
static void test_replay_failures(void) {
    char path[256];
    make_temp(path);
    {
        table t;
        CHECK(table_open(&t, path));
        for (int k = 0; k < 50; k++) {
            bool done;
            CHECK(table_insert(&t, k, (uint8_t *)"v", 1, &done) && done);
        }
        table_close(&t);
    }
    bool success = false;
    for (long fail = 0; fail < 500 && !success; fail++) {
        table t;
        remaining = fail;
        bool ok = table_open(&t, path);
        remaining = -1;
        if (!ok)
            continue;
        success = true;
        CHECK(table_range(&t, 0, 49, NULL, 0) == 50);
        for (int k = 0; k < 50; k++) {
            size_t len = 0;
            const uint8_t *v = table_get(&t, k, &len);
            CHECK(v && len == 1 && v[0] == 'v');
        }
        table_close(&t);
    }
    CHECK(success);
    CHECK(remove(path) == 0);
}
/* A failed insert leaves the log consistent: reopening shows either the
 * old table or the old table plus the logged row. */
static void test_insert_failures(void) {
    char path[256];
    make_temp(path);
    bool success = false;
    for (long fail = 0; fail < 50 && !success; fail++) {
        CHECK(remove(path) == 0);
        table t;
        CHECK(table_open(&t, path));
        for (int k = 0; k < 10; k++) {
            bool done;
            CHECK(table_insert(&t, k, (uint8_t *)"v", 1, &done) && done);
        }
        bool done = true;
        remaining = fail;
        bool ok = table_insert(&t, 100, (uint8_t *)"new", 3, &done);
        remaining = -1;
        if (ok) {
            CHECK(done);
            success = true;
            size_t len = 0;
            const uint8_t *v = table_get(&t, 100, &len);
            CHECK(v && len == 3 && memcmp(v, "new", 3) == 0);
            table_close(&t);
        } else {
            CHECK(!done);
            table_close(&t);
            CHECK(table_open(&t, path));
            CHECK(table_range(&t, 0, 9, NULL, 0) == 10);
            size_t len = 0;
            const uint8_t *v = table_get(&t, 100, &len);
            CHECK(v == NULL || (len == 3 && memcmp(v, "new", 3) == 0));
            table_close(&t);
        }
    }
    CHECK(success);
    CHECK(remove(path) == 0);
}
int main(void) {
    test_persistence();
    test_invalid_files();
    test_replay_failures();
    test_insert_failures();
}
