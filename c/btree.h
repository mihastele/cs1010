#ifndef BTREE_H
#define BTREE_H
#include <stdbool.h>
#include <stddef.h>
typedef struct btree_node btree_node;
typedef struct { btree_node *root; size_t degree, size; } btree;
/* Degree must be at least 2. A degree of 2 gives a 2-3-4 tree. */
bool btree_init(btree *tree, size_t degree);
void btree_destroy(btree *tree);
bool btree_contains(const btree *tree, int key);
/* Returns false on allocation failure. *inserted distinguishes duplicates. */
bool btree_insert(btree *tree, int key, bool *inserted);
bool btree_remove(btree *tree, int key);
#endif
