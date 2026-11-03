#include "btree.hpp"
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>
#include <stdexcept>

static void require(bool ok) { if (!ok) std::abort(); }
struct BTreeTest {
    static std::size_t check(const BTree::Node &n, std::size_t t, bool root, long long lo, long long hi, int depth, int &leaf_depth) {
        require(n.keys.size()<=2*t-1);
        if (!root) require(n.keys.size()>=t-1);
        if (root && !n.leaf()) require(!n.keys.empty());
        auto previous=lo;
        for (auto k:n.keys) { require(k>previous && k<hi); previous=k; }
        if (n.leaf()) { if (leaf_depth<0) leaf_depth=depth; require(leaf_depth==depth); return n.keys.size(); }
        require(n.children.size()==n.keys.size()+1);
        std::size_t count=n.keys.size();
        for (std::size_t i=0;i<n.children.size();++i)
            count+=check(*n.children[i],t,false,i?n.keys[i-1]:lo,i<n.keys.size()?n.keys[i]:hi,depth+1,leaf_depth);
        return count;
    }
    static void validate(const BTree &tree, std::size_t degree) {
        int depth=-1;
        require(check(tree.root_,degree,true,std::numeric_limits<long long>::min(),std::numeric_limits<long long>::max(),0,depth)==tree.size());
    }
    static void run() {
        for (std::size_t degree : {2,3,5,16}) {
            BTree tree(degree); std::set<int> expected; std::uint32_t state=42;
            for (int i=0;i<12000;++i) {
                state=state*1664525u+1013904223u;
                int key=static_cast<int>((state>>8)%300)-150;
                if (state&1) require(tree.remove(key)==(expected.erase(key)!=0));
                else require(tree.insert(key)==expected.insert(key).second);
                require(tree.size()==expected.size()); validate(tree,degree);
                for (int k=-150;k<150;++k) require(tree.contains(k)==(expected.count(k)!=0));
            }
            for (int k : expected) { require(tree.remove(k)); validate(tree,degree); }
            require(tree.empty());
            for (int k : {std::numeric_limits<int>::min(),0,std::numeric_limits<int>::max()}) { require(tree.insert(k)); require(!tree.insert(k)); }
            for (int k : {std::numeric_limits<int>::min(),0,std::numeric_limits<int>::max()}) { require(tree.remove(k)); require(!tree.remove(k)); }
        }
        Tree234 tree; require(tree.insert(1)); require(tree.contains(1)); require(tree.remove(1));
        try { BTree invalid(1); std::abort(); } catch (const std::invalid_argument&) {}
    }
};
int main() { BTreeTest::run(); }
