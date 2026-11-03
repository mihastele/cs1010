#include "bplus.hpp"
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>
static void require(bool ok) {
    if (!ok)
        std::abort();
}
struct BPlusTest {
    static std::vector<int> check(const BPlusTree::Node *n, std::size_t degree, bool root,
                                  int depth, int &leaf_depth,
                                  std::vector<const BPlusTree::Node *> &leaves) {
        require(n->keys.size() <= 2 * degree - 1);
        if (n->leaf()) {
            if (!root)
                require(n->keys.size() >= degree - 1);
            require(std::adjacent_find(n->keys.begin(), n->keys.end(), std::greater_equal<int>()) ==
                    n->keys.end());
            if (leaf_depth < 0)
                leaf_depth = depth;
            require(leaf_depth == depth);
            leaves.push_back(n);
            return n->keys;
        }
        require(n->children.size() >= (root ? 2 : degree));
        require(n->keys.size() + 1 == n->children.size());
        std::vector<int> all;
        for (std::size_t i = 0; i < n->children.size(); ++i) {
            auto keys = check(n->children[i].get(), degree, false, depth + 1, leaf_depth, leaves);
            require(!keys.empty());
            if (i) {
                require(n->keys[i - 1] == keys.front());
                require(all.back() < keys.front());
            }
            all.insert(all.end(), keys.begin(), keys.end());
        }
        return all;
    }
    static void validate(const BPlusTree &t, const std::set<int> &expected) {
        int depth = -1;
        std::vector<const BPlusTree::Node *> leaves;
        auto keys = check(t.root_.get(), t.degree_, true, 0, depth, leaves);
        require(keys == std::vector<int>(expected.begin(), expected.end()));
        require(t.size() == expected.size());
        for (std::size_t i = 0; i < leaves.size(); ++i)
            require(leaves[i]->next == (i + 1 < leaves.size() ? leaves[i + 1] : nullptr));
    }
    static void run() {
        for (std::size_t degree : {2, 3, 5, 16}) {
            BPlusTree t(degree);
            std::set<int> expected;
            std::uint32_t state = 42;
            for (int i = 0; i < 12000; ++i) {
                state = state * 1664525u + 1013904223u;
                int key = static_cast<int>((state >> 8) % 300) - 150;
                if (state & 1)
                    require(t.remove(key) == (expected.erase(key) != 0));
                else
                    require(t.insert(key) == expected.insert(key).second);
                validate(t, expected);
                require(t.range(key - 20, key + 20) ==
                        std::vector<int>(expected.lower_bound(key - 20),
                                         expected.upper_bound(key + 20)));
            }
            auto copy = expected;
            for (int key : copy) {
                require(t.remove(key));
                expected.erase(key);
                validate(t, expected);
            }
            require(t.empty());
            for (int key : {std::numeric_limits<int>::min(), 0, std::numeric_limits<int>::max()}) {
                require(t.insert(key));
                require(!t.insert(key));
            }
            require(t.range(2, 1).empty());
            require(
                t.range(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()).size() ==
                3);
            for (int key : {std::numeric_limits<int>::min(), 0, std::numeric_limits<int>::max()}) {
                require(t.remove(key));
                require(!t.remove(key));
            }
        }
    }
};
int main() {
    BPlusTest::run();
}
