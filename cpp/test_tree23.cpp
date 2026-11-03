#include "tree23.hpp"
#include <cstdint>
#include <cstdlib>
#include <set>
static void require(bool b) { if(!b) std::abort(); }
int main() {
    Tree23 tree; std::set<int> expected; std::uint32_t state=42;
    for(int i=0;i<12000;i++) {
        state=state*1664525u+1013904223u; int key=static_cast<int>((state>>8)%300);
        if(state&1) require(tree.remove(key)==(expected.erase(key)!=0));
        else require(tree.insert(key)==expected.insert(key).second);
        require(tree.size()==expected.size());
        for(int k=0;k<300;k++) require(tree.contains(k)==(expected.count(k)!=0));
    }
    for(int k:expected) require(tree.remove(k));
    require(tree.empty());
}
