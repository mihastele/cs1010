#ifndef TREE23_H
#define TREE23_H
#include "rb_tree.h"
#include <stddef.h>
/* A left-leaning red link joins two binary nodes into one 3-node. */
typedef struct {
    rb_tree tree;
    size_t size;
} tree23;
void tree23_init(tree23 *tree);
void tree23_destroy(tree23 *tree);
bool tree23_contains(const tree23 *tree, int key);
/* Returns false on allocation failure; *inserted reports a new key. */
bool tree23_insert(tree23 *tree, int key, bool *inserted);
bool tree23_remove(tree23 *tree, int key);
#endif
