#ifndef RB_TREE_H
#define RB_TREE_H
#include <stdbool.h>
typedef struct rb_node rb_node;
typedef struct {
    rb_node *root;
} rb_tree;
void rb_init(rb_tree *tree);
/* Returns false if allocation fails; duplicate keys are accepted without changes. */
bool rb_insert(rb_tree *tree, int key);
/* Returns whether the key was present. */
bool rb_remove(rb_tree *tree, int key);
bool rb_contains(const rb_tree *tree, int key);
void rb_destroy(rb_tree *tree);
#endif
