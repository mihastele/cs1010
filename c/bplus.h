#ifndef BPLUS_H
#define BPLUS_H
#include <stdbool.h>
#include <stddef.h>
typedef struct bplus_node bplus_node;
typedef struct {
    bplus_node *root;
    size_t degree, size;
} bplus_tree;
bool bplus_init(bplus_tree *tree, size_t degree);
void bplus_destroy(bplus_tree *tree);
bool bplus_contains(const bplus_tree *tree, int key);
/* Returns false on allocation failure; *inserted is false for duplicates. */
bool bplus_insert(bplus_tree *tree, int key, bool *inserted);
bool bplus_remove(bplus_tree *tree, int key);
/* Inclusive range. Returns the total number of matches; writes up to capacity. */
size_t bplus_range(const bplus_tree *tree, int first, int last, int *out, size_t capacity);
#endif
