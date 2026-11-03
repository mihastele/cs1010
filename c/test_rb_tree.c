#include <assert.h>
#include <limits.h>
#include <stdint.h>
/* Include implementation so the test can inspect private structural invariants. */
#include "rb_tree.c"
static int check(const rb_node *n, long long lo, long long hi) {
    if (!n) return 1;
    assert(n->key>lo && n->key<hi); assert(!red(n->right));
    if(n->red) assert(!red(n->left));
    int l=check(n->left,lo,n->key), r=check(n->right,n->key,hi);
    assert(l==r); return l+!n->red;
}
int main(void) {
    for(int reverse=0;reverse<2;reverse++) {
        rb_tree t; rb_init(&t); assert(!rb_contains(&t,0));
        for(int i=0;i<1000;i++) { int k=reverse?999-i:i; assert(rb_insert(&t,k)); assert(rb_insert(&t,k)); assert(!red(t.root)); check(t.root,LLONG_MIN,LLONG_MAX); }
        for(int i=0;i<1000;i++) assert(rb_contains(&t,i));
        assert(!rb_contains(&t,-1)); assert(!rb_contains(&t,1000));
        for(int i=0;i<1000;i++) { int k=(i*337)%1000; assert(rb_remove(&t,k)); assert(!rb_remove(&t,k)); assert(!red(t.root)); check(t.root,LLONG_MIN,LLONG_MAX); }
        assert(!t.root); rb_destroy(&t);
    }
    {
        rb_tree t; rb_init(&t); bool expected[200]={false}; uint32_t state=42;
        for(int i=0;i<10000;i++) {
            state=state*1664525u+1013904223u; int k=(int)((state>>8)%200);
            if(state&1) { assert(rb_remove(&t,k)==expected[k]); expected[k]=false; }
            else { assert(rb_insert(&t,k)); expected[k]=true; }
            assert(!red(t.root)); check(t.root,LLONG_MIN,LLONG_MAX);
            for(int j=0;j<200;j++) assert(rb_contains(&t,j)==expected[j]);
        }
        rb_destroy(&t);
    }
}
