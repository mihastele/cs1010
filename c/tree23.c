#include "tree23.h"
void tree23_init(tree23 *t) { rb_init(&t->tree); t->size=0; }
void tree23_destroy(tree23 *t) { rb_destroy(&t->tree); t->size=0; }
bool tree23_contains(const tree23 *t, int key) { return rb_contains(&t->tree,key); }
bool tree23_insert(tree23 *t, int key, bool *inserted) {
    *inserted=false; if(tree23_contains(t,key)) return true;
    if(!rb_insert(&t->tree,key)) return false;
    t->size++; *inserted=true; return true;
}
bool tree23_remove(tree23 *t, int key) {
    if(!rb_remove(&t->tree,key)) return false;
    t->size--; return true;
}
