#include "rb_tree.h"
#include <stdlib.h>
struct rb_node { int key; bool red; rb_node *left, *right; };
static bool red(const rb_node *n) { return n && n->red; }
static rb_node *left(rb_node *h) { rb_node *x=h->right; h->right=x->left; x->left=h; x->red=h->red; h->red=true; return x; }
static rb_node *right(rb_node *h) { rb_node *x=h->left; h->left=x->right; x->right=h; x->red=h->red; h->red=true; return x; }
/* Left-leaning red links encode 3-nodes; rotations and color flips restore invariants. */
static rb_node *put(rb_node *h, int key, bool *ok) {
    if (!h) { h=malloc(sizeof *h); if (!h) { *ok=false; return NULL; } *h=(rb_node){key,true,NULL,NULL}; return h; }
    if (key<h->key) h->left=put(h->left,key,ok);
    else if (key>h->key) h->right=put(h->right,key,ok);
    if (!*ok) return h;
    if (red(h->right) && !red(h->left)) h=left(h);
    if (red(h->left) && red(h->left->left)) h=right(h);
    if (red(h->left) && red(h->right)) { h->red=!h->red; h->left->red=false; h->right->red=false; }
    return h;
}
static void flip(rb_node *h) { h->red=!h->red; if(h->left) h->left->red=!h->left->red; if(h->right) h->right->red=!h->right->red; }
/* Move a red link towards the descent before removing from a 2-node. */
static rb_node *move_left(rb_node *h) {
    flip(h); if(h->right && red(h->right->left)) { h->right=right(h->right); h=left(h); flip(h); } return h;
}
static rb_node *move_right(rb_node *h) {
    flip(h); if(h->left && red(h->left->left)) { h=right(h); flip(h); } return h;
}
static rb_node *balance(rb_node *h) {
    if(red(h->right)) h=left(h);
    if(red(h->left) && red(h->left->left)) h=right(h);
    if(red(h->left) && red(h->right)) flip(h);
    return h;
}
static rb_node *delete_min(rb_node *h) {
    if(!h->left) { free(h); return NULL; }
    if(!red(h->left) && !red(h->left->left)) h=move_left(h);
    h->left=delete_min(h->left); return balance(h);
}
static rb_node *delete_key(rb_node *h, int key) {
    if(key<h->key) {
        if(!red(h->left) && !red(h->left->left)) h=move_left(h);
        h->left=delete_key(h->left,key);
    } else {
        if(red(h->left)) h=right(h);
        if(key==h->key && !h->right) { free(h); return NULL; }
        if(!red(h->right) && !red(h->right->left)) h=move_right(h);
        if(key==h->key) { rb_node *n=h->right; while(n->left) n=n->left; h->key=n->key; h->right=delete_min(h->right); }
        else h->right=delete_key(h->right,key);
    }
    return balance(h);
}
void rb_init(rb_tree *t) { t->root=NULL; }
bool rb_insert(rb_tree *t, int key) { bool ok=true; t->root=put(t->root,key,&ok); if (t->root) t->root->red=false; return ok; }
bool rb_contains(const rb_tree *t, int key) { const rb_node *n=t->root; while(n) { if(key==n->key) return true; n=key<n->key?n->left:n->right; } return false; }
bool rb_remove(rb_tree *t, int key) {
    if(!rb_contains(t,key)) return false;
    if(!red(t->root->left) && !red(t->root->right)) t->root->red=true;
    t->root=delete_key(t->root,key); if(t->root) t->root->red=false; return true;
}
static void destroy(rb_node *n) { if(n) { destroy(n->left); destroy(n->right); free(n); } }
void rb_destroy(rb_tree *t) { destroy(t->root); t->root=NULL; }
