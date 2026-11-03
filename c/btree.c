#include "btree.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
struct btree_node { size_t count; int *keys; struct btree_node **children; bool leaf; };
static btree_node *node_new(size_t t) {
    btree_node *n=calloc(1,sizeof *n);
    if (!n) return NULL;
    n->keys=malloc((2*t-1)*sizeof *n->keys);
    n->children=calloc(2*t,sizeof *n->children);
    if (!n->keys || !n->children) { free(n->keys); free(n->children); free(n); return NULL; }
    n->leaf=true; return n;
}
static void node_free(btree_node *n) { free(n->keys); free(n->children); free(n); }
static void destroy(btree_node *n) {
    if (!n) return;
    if (!n->leaf) for(size_t i=0;i<=n->count;i++) destroy(n->children[i]);
    node_free(n);
}
bool btree_init(btree *tree, size_t degree) {
    if (degree<2 || degree>SIZE_MAX/2 || 2*degree>SIZE_MAX/sizeof(btree_node *) || (2*degree-1)>SIZE_MAX/sizeof(int)) return false;
    tree->root=NULL; tree->degree=degree; tree->size=0; return true;
}
void btree_destroy(btree *tree) { destroy(tree->root); tree->root=NULL; tree->size=0; }
static size_t lower(const btree_node *n, int key) { size_t i=0; while(i<n->count && n->keys[i]<key) i++; return i; }
bool btree_contains(const btree *tree, int key) {
    const btree_node *n=tree->root;
    while(n) { size_t i=lower(n,key); if(i<n->count && n->keys[i]==key) return true; n=n->leaf?NULL:n->children[i]; }
    return false;
}
/* Insertion reserves enough nodes for every possible split before mutating
   the tree, so an allocation failure leaves all existing keys intact. */
typedef struct { btree_node **nodes; size_t count; } pool;
static btree_node *take(pool *p) { return p->nodes[--p->count]; }
static void split(btree_node *parent, size_t i, size_t t, pool *p) {
    btree_node *child=parent->children[i], *right=take(p);
    right->leaf=child->leaf; right->count=t-1;
    memcpy(right->keys,child->keys+t,(t-1)*sizeof(int));
    if(!child->leaf) memcpy(right->children,child->children+t,t*sizeof(btree_node *));
    int median=child->keys[t-1]; child->count=t-1;
    memmove(parent->keys+i+1,parent->keys+i,(parent->count-i)*sizeof(int));
    memmove(parent->children+i+2,parent->children+i+1,(parent->count-i)*sizeof(btree_node *));
    parent->keys[i]=median; parent->children[i+1]=right; parent->count++;
}
static void insert_nonfull(btree_node *n, int key, size_t t, pool *p) {
    size_t i=lower(n,key);
    if(n->leaf) { memmove(n->keys+i+1,n->keys+i,(n->count-i)*sizeof(int)); n->keys[i]=key; n->count++; return; }
    if(n->children[i]->count==2*t-1) { split(n,i,t,p); if(key>n->keys[i]) i++; }
    insert_nonfull(n->children[i],key,t,p);
}
bool btree_insert(btree *tree, int key, bool *inserted) {
    *inserted=false;
    if(btree_contains(tree,key)) return true;
    size_t height=0; for(btree_node *n=tree->root;n;n=n->leaf?NULL:n->children[0]) height++;
    if(height>SIZE_MAX-2 || height+2>SIZE_MAX/sizeof(btree_node *)) return false;
    pool p={malloc((height+2)*sizeof(btree_node *)),0};
    if(!p.nodes) return false;
    for(size_t i=0;i<height+2;i++) {
        btree_node *n=node_new(tree->degree);
        if(!n) { while(p.count) node_free(take(&p)); free(p.nodes); return false; }
        p.nodes[p.count++]=n;
    }
    if(!tree->root) tree->root=take(&p);
    if(tree->root->count==2*tree->degree-1) {
        btree_node *root=take(&p); root->leaf=false; root->children[0]=tree->root;
        split(root,0,tree->degree,&p); tree->root=root;
    }
    insert_nonfull(tree->root,key,tree->degree,&p); tree->size++; *inserted=true;
    while(p.count) node_free(take(&p)); free(p.nodes); return true;
}
static void merge(btree_node *n, size_t i) {
    btree_node *l=n->children[i], *r=n->children[i+1]; size_t at=l->count;
    l->keys[at]=n->keys[i]; memcpy(l->keys+at+1,r->keys,r->count*sizeof(int));
    if(!l->leaf) memcpy(l->children+at+1,r->children,(r->count+1)*sizeof(btree_node *));
    l->count+=r->count+1;
    memmove(n->keys+i,n->keys+i+1,(n->count-i-1)*sizeof(int));
    memmove(n->children+i+1,n->children+i+2,(n->count-i-1)*sizeof(btree_node *));
    n->count--; node_free(r);
}
/* Borrow or merge before descending into a minimally occupied child. */
static size_t prepare(btree_node *n, size_t i, size_t t) {
    btree_node *c=n->children[i], *s;
    if(c->count>=t) return i;
    if(i>0 && n->children[i-1]->count>=t) {
        s=n->children[i-1]; memmove(c->keys+1,c->keys,c->count*sizeof(int)); c->keys[0]=n->keys[i-1];
        n->keys[i-1]=s->keys[--s->count];
        if(!c->leaf) { memmove(c->children+1,c->children,(c->count+1)*sizeof(btree_node *)); c->children[0]=s->children[s->count+1]; }
        c->count++;
    } else if(i<n->count && n->children[i+1]->count>=t) {
        s=n->children[i+1]; c->keys[c->count]=n->keys[i]; n->keys[i]=s->keys[0];
        memmove(s->keys,s->keys+1,(s->count-1)*sizeof(int));
        if(!c->leaf) { c->children[c->count+1]=s->children[0]; memmove(s->children,s->children+1,s->count*sizeof(btree_node *)); }
        s->count--; c->count++;
    } else if(i<n->count) merge(n,i);
    else { --i; merge(n,i); }
    return i;
}
static void remove_key(btree_node *n, int key, size_t t) {
    size_t i=lower(n,key);
    if(i<n->count && n->keys[i]==key) {
        if(n->leaf) { memmove(n->keys+i,n->keys+i+1,(n->count-i-1)*sizeof(int)); n->count--; }
        else if(n->children[i]->count>=t) {
            btree_node *p=n->children[i]; while(!p->leaf) p=p->children[p->count];
            int k=p->keys[p->count-1]; n->keys[i]=k; remove_key(n->children[i],k,t);
        } else if(n->children[i+1]->count>=t) {
            btree_node *p=n->children[i+1]; while(!p->leaf) p=p->children[0];
            int k=p->keys[0]; n->keys[i]=k; remove_key(n->children[i+1],k,t);
        } else { merge(n,i); remove_key(n->children[i],key,t); }
    } else if(!n->leaf) { i=prepare(n,i,t); remove_key(n->children[i],key,t); }
}
bool btree_remove(btree *tree, int key) {
    if(!btree_contains(tree,key)) return false;
    remove_key(tree->root,key,tree->degree); tree->size--;
    if(tree->root->count==0) { btree_node *old=tree->root; tree->root=old->leaf?NULL:old->children[0]; node_free(old); }
    return true;
}
