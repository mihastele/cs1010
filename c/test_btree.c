#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
/* Inject allocation failures while inspecting internal invariants. */
static long allocations_before_failure=-1;
static void *test_malloc(size_t size) {
    if(allocations_before_failure==0) return NULL;
    if(allocations_before_failure>0) --allocations_before_failure;
    return malloc(size);
}
static void *test_calloc(size_t count, size_t size) {
    if(allocations_before_failure==0) return NULL;
    if(allocations_before_failure>0) --allocations_before_failure;
    return calloc(count,size);
}
#define malloc test_malloc
#define calloc test_calloc
#include "btree.c"
#undef malloc
#undef calloc
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed: %s at line %d\n",#x,__LINE__); abort(); } } while(0)
static size_t check(const btree_node *n, size_t t, bool root, long long lo, long long hi, int depth, int *leaf_depth) {
    if(!n) { CHECK(root); return 0; }
    CHECK(n->count<=2*t-1); if(!root) CHECK(n->count>=t-1); else CHECK(n->count>0);
    long long prev=lo;
    for(size_t i=0;i<n->count;i++) { CHECK(n->keys[i]>prev && n->keys[i]<hi); prev=n->keys[i]; }
    if(n->leaf) { if(*leaf_depth<0) *leaf_depth=depth; CHECK(*leaf_depth==depth); return n->count; }
    size_t count=n->count;
    for(size_t i=0;i<=n->count;i++) {
        CHECK(n->children[i]);
        count+=check(n->children[i],t,false,i?n->keys[i-1]:lo,i<n->count?n->keys[i]:hi,depth+1,leaf_depth);
    }
    return count;
}
static void validate(const btree *tree) { int depth=-1; CHECK(check(tree->root,tree->degree,true,LLONG_MIN,LLONG_MAX,0,&depth)==tree->size); }
int main(void) {
    btree invalid; CHECK(!btree_init(&invalid,1)); CHECK(!btree_init(&invalid,SIZE_MAX));
    bool succeeded=false;
    for(long fail=0;fail<100 && !succeeded;fail++) {
        btree tree; CHECK(btree_init(&tree,2));
        for(int k=0;k<50;k++) { bool added; CHECK(btree_insert(&tree,k,&added) && added); }
        allocations_before_failure=fail;
        bool added=true; bool ok=btree_insert(&tree,100,&added);
        allocations_before_failure=-1;
        validate(&tree);
        for(int k=0;k<50;k++) CHECK(btree_contains(&tree,k));
        if(ok) { CHECK(added && btree_contains(&tree,100) && tree.size==51); succeeded=true; }
        else { CHECK(!added && !btree_contains(&tree,100) && tree.size==50); }
        btree_destroy(&tree);
    }
    CHECK(succeeded);
    const size_t degrees[]={2,3,5,16};
    for(size_t d=0;d<sizeof degrees/sizeof *degrees;d++) {
        btree tree; CHECK(btree_init(&tree,degrees[d])); bool expected[300]={false}; uint32_t state=42;
        for(int i=0;i<12000;i++) {
            state=state*1664525u+1013904223u; int key=(int)((state>>8)%300); bool inserted;
            if(state&1) { CHECK(btree_remove(&tree,key)==expected[key]); expected[key]=false; }
            else { CHECK(btree_insert(&tree,key,&inserted)); CHECK(inserted==!expected[key]); expected[key]=true; }
            validate(&tree);
            for(int k=0;k<300;k++) CHECK(btree_contains(&tree,k)==expected[k]);
        }
        for(int k=0;k<300;k++) if(expected[k]) { CHECK(btree_remove(&tree,k)); validate(&tree); }
        CHECK(tree.size==0 && tree.root==NULL);
        int limits[]={INT_MIN,0,INT_MAX};
        for(size_t i=0;i<3;i++) { bool added; CHECK(btree_insert(&tree,limits[i],&added) && added); CHECK(btree_insert(&tree,limits[i],&added) && !added); }
        for(size_t i=0;i<3;i++) { CHECK(btree_remove(&tree,limits[i])); CHECK(!btree_remove(&tree,limits[i])); }
        btree_destroy(&tree);
    }
}
