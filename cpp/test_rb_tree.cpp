#include "rb_tree.hpp"
#include <cassert>
#include <limits>
#include <initializer_list>
#include <cstdint>
#include <set>
struct TreeTest {
    static int check(const RedBlackTree::Link& n, long long lo, long long hi) {
        if (!n) return 1;
        assert(n->key>lo && n->key<hi); assert(!RedBlackTree::red(n->right));
        if(n->red) assert(!RedBlackTree::red(n->left));
        int l=check(n->left,lo,n->key), r=check(n->right,n->key,hi);
        assert(l==r); return l+!n->red;
    }
    static void run() {
        for(bool reverse : {false,true}) {
            RedBlackTree t; assert(!t.contains(0));
            for(int i=0;i<1000;i++) { int k=reverse?999-i:i; t.insert(k); t.insert(k); assert(!RedBlackTree::red(t.root)); check(t.root, std::numeric_limits<long long>::min(), std::numeric_limits<long long>::max()); }
            for(int i=0;i<1000;i++) assert(t.contains(i));
            assert(!t.contains(-1)); assert(!t.contains(1000));
            for(int i=0;i<1000;i++) { int k=(i*337)%1000; assert(t.remove(k)); assert(!t.remove(k)); assert(!RedBlackTree::red(t.root)); check(t.root,std::numeric_limits<long long>::min(),std::numeric_limits<long long>::max()); }
            assert(!t.root);
        }
        RedBlackTree t; std::set<int> expected; std::uint32_t state=42;
        for(int i=0;i<10000;i++) {
            state=state*1664525u+1013904223u; int k=static_cast<int>((state>>8)%200);
            if(state&1) { assert(t.remove(k)==(expected.erase(k)!=0)); }
            else { t.insert(k); expected.insert(k); }
            assert(!RedBlackTree::red(t.root)); check(t.root,std::numeric_limits<long long>::min(),std::numeric_limits<long long>::max());
            for(int j=0;j<200;j++) assert(t.contains(j)==(expected.count(j)!=0));
        }
    }
};
int main() { TreeTest::run(); }
