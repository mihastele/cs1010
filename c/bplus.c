#include "bplus.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Internal keys copy the minimum key in each child after the first. */
struct bplus_node {
    size_t count;
    int *keys;
    struct bplus_node **children, *next;
    bool leaf;
};
static bplus_node *node_new(size_t t) {
    bplus_node *n = calloc(1, sizeof *n);
    if (!n)
        return NULL;
    /* One extra key and child allow temporary overflow before splitting. */
    n->keys = malloc(2 * t * sizeof *n->keys);
    n->children = calloc(2 * t + 1, sizeof *n->children);
    if (!n->keys || !n->children) {
        free(n->keys);
        free(n->children);
        free(n);
        return NULL;
    }
    n->leaf = true;
    return n;
}
static void node_free(bplus_node *n) {
    free(n->keys);
    free(n->children);
    free(n);
}
static void destroy(bplus_node *n) {
    if (!n)
        return;
    if (!n->leaf)
        for (size_t i = 0; i <= n->count; i++)
            destroy(n->children[i]);
    node_free(n);
}
bool bplus_init(bplus_tree *t, size_t degree) {
    if (degree < 2 || degree > (SIZE_MAX - 1) / 2 || 2 * degree > SIZE_MAX / sizeof(int) ||
        2 * degree + 1 > SIZE_MAX / sizeof(bplus_node *))
        return false;
    t->root = NULL;
    t->degree = degree;
    t->size = 0;
    return true;
}
void bplus_destroy(bplus_tree *t) {
    destroy(t->root);
    t->root = NULL;
    t->size = 0;
}
static size_t lower(const bplus_node *n, int key) {
    size_t i = 0;
    while (i < n->count && n->keys[i] < key)
        i++;
    return i;
}
static size_t child_index(const bplus_node *n, int key) {
    size_t i = 0;
    while (i < n->count && n->keys[i] <= key)
        i++;
    return i;
}
static const bplus_node *leaf_for(const bplus_tree *t, int key) {
    const bplus_node *n = t->root;
    while (n && !n->leaf)
        n = n->children[child_index(n, key)];
    return n;
}
bool bplus_contains(const bplus_tree *t, int key) {
    const bplus_node *n = leaf_for(t, key);
    if (!n)
        return false;
    size_t i = lower(n, key);
    return i < n->count && n->keys[i] == key;
}
static int minimum(const bplus_node *n) {
    while (!n->leaf)
        n = n->children[0];
    return n->keys[0];
}
static void refresh(bplus_node *n) {
    if (!n->leaf)
        for (size_t i = 0; i < n->count; i++)
            n->keys[i] = minimum(n->children[i + 1]);
}
typedef struct {
    bplus_node **nodes;
    size_t count;
} pool;
static bplus_node *take(pool *p) {
    return p->nodes[--p->count];
}
/* Bottom-up split. The pool is allocated before changing the tree. */
/* Unlike a B-tree split, the separator stays in the right leaf:
 *    [a b c d]  ->  [a b] -> [c d]    parent separator: c
 * The linked leaves contain every key; internal keys are routing copies. */
static bplus_node *insert_node(bplus_node *n, int key, size_t t, pool *p) {
    if (n->leaf) {
        size_t i = lower(n, key);
        memmove(n->keys + i + 1, n->keys + i, (n->count - i) * sizeof(int));
        n->keys[i] = key;
        n->count++;
        if (n->count < 2 * t)
            return NULL;
        bplus_node *right = take(p);
        right->count = t;
        memcpy(right->keys, n->keys + t, t * sizeof(int));
        n->count = t;
        right->next = n->next;
        n->next = right;
        return right;
    }
    size_t i = child_index(n, key);
    bplus_node *right = insert_node(n->children[i], key, t, p);
    if (right) {
        memmove(n->children + i + 2, n->children + i + 1, (n->count - i) * sizeof(bplus_node *));
        n->children[i + 1] = right;
        n->count++;
    }
    refresh(n);
    if (n->count < 2 * t)
        return NULL;
    bplus_node *sibling = take(p);
    sibling->leaf = false;
    /* Overflow has 2t+1 children; leave t on the left. */
    sibling->count = t;
    memcpy(sibling->children, n->children + t, (t + 1) * sizeof(bplus_node *));
    n->count = t - 1;
    refresh(n);
    refresh(sibling);
    return sibling;
}
bool bplus_insert(bplus_tree *tree, int key, bool *inserted) {
    *inserted = false;
    if (bplus_contains(tree, key))
        return true;
    size_t height = 0;
    for (bplus_node *n = tree->root; n; n = n->leaf ? NULL : n->children[0])
        height++;
    if (height > SIZE_MAX - 2 || height + 2 > SIZE_MAX / sizeof(bplus_node *))
        return false;
    pool p = {malloc((height + 2) * sizeof(bplus_node *)), 0};
    if (!p.nodes)
        return false;
    for (size_t i = 0; i < height + 2; i++) {
        bplus_node *n = node_new(tree->degree);
        if (!n) {
            while (p.count)
                node_free(take(&p));
            free(p.nodes);
            return false;
        }
        p.nodes[p.count++] = n;
    }
    if (!tree->root)
        tree->root = take(&p);
    bplus_node *right = insert_node(tree->root, key, tree->degree, &p);
    if (right) {
        bplus_node *root = take(&p);
        root->leaf = false;
        root->count = 1;
        root->children[0] = tree->root;
        root->children[1] = right;
        refresh(root);
        tree->root = root;
    }
    tree->size++;
    *inserted = true;
    while (p.count)
        node_free(take(&p));
    free(p.nodes);
    return true;
}
static bool underfull(const bplus_node *n, size_t t) {
    return n->leaf ? n->count < t - 1 : n->count + 1 < t;
}
static bool can_lend(const bplus_node *n, size_t t) {
    return n->leaf ? n->count > t - 1 : n->count + 1 > t;
}
/* Repair the deficient child by borrowing or merging, then rebuild separators. */
/* Leaf borrowing moves a data key; internal borrowing moves a child.
 * Merging leaves also skips the removed leaf in the linked list. The caller
 * refreshes the parent's routing copies after either operation. */
static void repair(bplus_node *p, size_t i, size_t t) {
    bplus_node *c = p->children[i], *s;
    if (!underfull(c, t))
        return;
    if (i > 0 && can_lend(p->children[i - 1], t)) {
        s = p->children[i - 1];
        if (c->leaf) {
            memmove(c->keys + 1, c->keys, c->count * sizeof(int));
            c->keys[0] = s->keys[--s->count];
            c->count++;
        } else {
            memmove(c->children + 1, c->children, (c->count + 1) * sizeof(bplus_node *));
            c->children[0] = s->children[s->count];
            s->count--;
            c->count++;
            refresh(s);
            refresh(c);
        }
    } else if (i < p->count && can_lend(p->children[i + 1], t)) {
        s = p->children[i + 1];
        if (c->leaf) {
            c->keys[c->count++] = s->keys[0];
            s->count--;
            memmove(s->keys, s->keys + 1, s->count * sizeof(int));
        } else {
            c->children[c->count + 1] = s->children[0];
            c->count++;
            memmove(s->children, s->children + 1, s->count * sizeof(bplus_node *));
            s->count--;
            refresh(s);
            refresh(c);
        }
    } else {
        size_t j = i ? i - 1 : i;
        bplus_node *left = p->children[j], *right = p->children[j + 1];
        if (left->leaf) {
            memcpy(left->keys + left->count, right->keys, right->count * sizeof(int));
            left->count += right->count;
            left->next = right->next;
        } else {
            memcpy(left->children + left->count + 1, right->children,
                   (right->count + 1) * sizeof(bplus_node *));
            left->count += right->count + 1;
            refresh(left);
        }
        memmove(p->children + j + 1, p->children + j + 2,
                (p->count - j - 1) * sizeof(bplus_node *));
        p->count--;
        node_free(right);
    }
}
static void remove_node(bplus_node *n, int key, size_t t) {
    if (n->leaf) {
        size_t i = lower(n, key);
        memmove(n->keys + i, n->keys + i + 1, (n->count - i - 1) * sizeof(int));
        n->count--;
        return;
    }
    size_t i = child_index(n, key);
    remove_node(n->children[i], key, t);
    repair(n, i, t);
    refresh(n);
}
bool bplus_remove(bplus_tree *tree, int key) {
    if (!bplus_contains(tree, key))
        return false;
    remove_node(tree->root, key, tree->degree);
    tree->size--;
    if (tree->root->count == 0) {
        bplus_node *old = tree->root;
        tree->root = old->leaf ? NULL : old->children[0];
        node_free(old);
    }
    return true;
}
size_t bplus_range(const bplus_tree *tree, int first, int last, int *out, size_t capacity) {
    if (first > last)
        return 0;
    size_t count = 0;
    for (const bplus_node *n = leaf_for(tree, first); n; n = n->next)
        for (size_t i = 0; i < n->count; i++) {
            int key = n->keys[i];
            if (key > last)
                return count;
            if (key >= first) {
                if (count < capacity)
                    out[count] = key;
                count++;
            }
        }
    return count;
}
