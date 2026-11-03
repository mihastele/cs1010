#include "tree23.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "check failed: %s, line %d\n", #x, __LINE__);                          \
            abort();                                                                               \
        }                                                                                          \
    } while (0)
int main(void) {
    tree23 tree;
    tree23_init(&tree);
    bool expected[300] = {false};
    uint32_t state = 42;
    size_t count = 0;
    for (int i = 0; i < 12000; i++) {
        state = state * 1664525u + 1013904223u;
        int key = (int)((state >> 8) % 300);
        bool added;
        if (state & 1) {
            CHECK(tree23_remove(&tree, key) == expected[key]);
            if (expected[key])
                count--;
            expected[key] = false;
        } else {
            CHECK(tree23_insert(&tree, key, &added));
            CHECK(added == !expected[key]);
            if (added)
                count++;
            expected[key] = true;
        }
        CHECK(tree.size == count);
        for (int k = 0; k < 300; k++)
            CHECK(tree23_contains(&tree, k) == expected[k]);
    }
    tree23_destroy(&tree);
    CHECK(tree.size == 0);
}
