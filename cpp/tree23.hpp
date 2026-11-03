#pragma once
#include "rb_tree.hpp"
#include <cstddef>
// A red left link joins two binary nodes into a single 3-node.
class Tree23 {
    RedBlackTree tree_;
    std::size_t size_ = 0;

  public:
    bool contains(int key) const {
        return tree_.contains(key);
    }
    std::size_t size() const {
        return size_;
    }
    bool empty() const {
        return size_ == 0;
    }
    bool insert(int key) {
        if (contains(key))
            return false;
        tree_.insert(key);
        ++size_;
        return true;
    }
    bool remove(int key) {
        if (!tree_.remove(key))
            return false;
        --size_;
        return true;
    }
};
