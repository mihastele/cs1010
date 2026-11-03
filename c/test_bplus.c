#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static long remaining=-1;
static void *test_malloc(size_t n) { if(remaining==0) return NULL; if(remaining>0) --remaining; return malloc(n); }
static void *test_calloc(size_t n,size_t s) { if(remaining==0) return NULL; if(remaining>0) --remaining; return calloc(n,s); }
#define malloc test_malloc
#define calloc test_calloc
#include "bplus.c"
#undef malloc
#undef calloc
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"failed: %s at line %d\n",#x,__LINE__); abort(); } } while(0)
typedef struct { int depth; const bplus_node *previous; size_t count; long long last; } audit;
static void check(const bplus_node *n,size_t t,bool root,int depth,audit *a) {
    CHECK(n && n->count<=2*t-1);
    if(n->leaf) {
        if(!root) CHECK(n->count>=t-1);
        if(a->depth<0) a->depth=depth; CHECK(a->depth==depth);
        if(a->previous) CHECK(a->previous->next==n); a->previous=n;
        for(size_t i=0;i<n->count;i++) { CHECK(n->keys[i]>a->last); a->last=n->keys[i]; a->count++; }
    } else {
        CHECK(root?n->count>=1:n->count+1>=t);
        for(size_t i=0;i<=n->count;i++) { CHECK(n->children[i]); if(i) CHECK(n->keys[i-1]==minimum(n->children[i])); check(n->children[i],t,false,depth+1,a); }
    }
}
static void validate(const bplus_tree *t,const bool *expected) {
    audit a={-1,NULL,0,LLONG_MIN}; if(t->root) check(t->root,t->degree,true,0,&a);
    if(a.previous) CHECK(!a.previous->next); CHECK(a.count==t->size);
    for(int i=0;i<300;i++) CHECK(bplus_contains(t,i)==expected[i]);
}
int main(void) {
    bplus_tree invalid; CHECK(!bplus_init(&invalid,1)); CHECK(!bplus_init(&invalid,SIZE_MAX));
    bool success=false;
    for(long fail=0;fail<100 && !success;fail++) {
        bplus_tree t; bool expected[300]={false}; CHECK(bplus_init(&t,2));
        for(int i=0;i<50;i++) { bool inserted; CHECK(bplus_insert(&t,i,&inserted) && inserted); expected[i]=true; }
        bool inserted=true; remaining=fail; bool ok=bplus_insert(&t,100,&inserted); remaining=-1;
        if(ok) { CHECK(inserted); expected[100]=true; success=true; } else CHECK(!inserted);
        validate(&t,expected); bplus_destroy(&t);
    }
    CHECK(success);
    const size_t degrees[]={2,3,5,16};
    for(size_t d=0;d<4;d++) {
        bplus_tree t; bool expected[300]={false}; uint32_t state=42; CHECK(bplus_init(&t,degrees[d]));
        for(int i=0;i<12000;i++) {
            state=state*1664525u+1013904223u; int key=(int)((state>>8)%300); bool inserted;
            if(state&1) { CHECK(bplus_remove(&t,key)==expected[key]); expected[key]=false; }
            else { CHECK(bplus_insert(&t,key,&inserted)); CHECK(inserted==!expected[key]); expected[key]=true; }
            validate(&t,expected);
            int out[10]; size_t count=bplus_range(&t,key-20,key+20,out,10), actual=0;
            for(int k=0;k<300;k++) if(expected[k] && k>=key-20 && k<=key+20) { if(actual<10) CHECK(out[actual]==k); actual++; }
            CHECK(count==actual); CHECK(bplus_range(&t,key-20,key+20,NULL,0)==actual);
        }
        for(int k=0;k<300;k++) if(expected[k]) { CHECK(bplus_remove(&t,k)); expected[k]=false; validate(&t,expected); }
        CHECK(!t.root && t.size==0); int limits[]={INT_MIN,0,INT_MAX};
        for(int i=0;i<3;i++) { bool inserted; CHECK(bplus_insert(&t,limits[i],&inserted) && inserted); CHECK(bplus_insert(&t,limits[i],&inserted) && !inserted); }
        int out[3]; CHECK(bplus_range(&t,INT_MIN,INT_MAX,out,3)==3); for(int i=0;i<3;i++) CHECK(out[i]==limits[i]);
        CHECK(bplus_range(&t,1,0,NULL,0)==0);
        for(int i=0;i<3;i++) { CHECK(bplus_remove(&t,limits[i])); CHECK(!bplus_remove(&t,limits[i])); }
        bplus_destroy(&t);
    }
}
