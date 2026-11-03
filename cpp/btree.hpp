#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

// A B-tree set. Except for the root, each node holds t-1 to 2t-1 keys.
class BTree {
    struct Node {
        std::vector<int> keys;
        std::vector<std::unique_ptr<Node>> children;
        bool leaf() const {
            return children.empty();
        }
        std::size_t lower(int key) const {
            return std::lower_bound(keys.begin(), keys.end(), key) - keys.begin();
        }
        bool contains(int key) const {
            auto i = lower(key);
            return (i < keys.size() && keys[i] == key) || (!leaf() && children[i]->contains(key));
        }
        // A full child splits around its median, which moves to the parent:
        //       [p]                         [m | p]
        //      /   \           ->           /   |   \  .
        // [a ... m ... z]                [a...] [...z]
        void split(std::size_t i, std::size_t t) {
            auto &child = *children[i];
            auto right = std::make_unique<Node>();
            right->keys.assign(child.keys.begin() + t, child.keys.end());
            int median = child.keys[t - 1];
            child.keys.resize(t - 1);
            if (!child.leaf()) {
                for (std::size_t j = t; j < child.children.size(); ++j)
                    right->children.push_back(std::move(child.children[j]));
                child.children.resize(t);
            }
            keys.insert(keys.begin() + i, median);
            children.insert(children.begin() + i + 1, std::move(right));
        }
        void insert_nonfull(int key, std::size_t t) {
            auto i = lower(key);
            if (leaf()) {
                keys.insert(keys.begin() + i, key);
                return;
            }
            if (children[i]->keys.size() == 2 * t - 1) {
                split(i, t);
                if (key > keys[i])
                    ++i;
            }
            children[i]->insert_nonfull(key, t);
        }
        // Pull the parent separator down between the two siblings. Their
        // combined node remains within the maximum capacity of 2t-1 keys.
        void merge(std::size_t i) {
            auto right = std::move(children[i + 1]);
            auto &left = *children[i];
            left.keys.push_back(keys[i]);
            left.keys.insert(left.keys.end(), right->keys.begin(), right->keys.end());
            for (auto &child : right->children)
                left.children.push_back(std::move(child));
            keys.erase(keys.begin() + i);
            children.erase(children.begin() + i + 1);
        }
        // Give a minimally occupied child another key before descending.
        std::size_t prepare(std::size_t i, std::size_t t) {
            if (children[i]->keys.size() >= t)
                return i;
            if (i > 0 && children[i - 1]->keys.size() >= t) {
                auto &s = *children[i - 1], &c = *children[i];
                c.keys.insert(c.keys.begin(), keys[i - 1]);
                keys[i - 1] = s.keys.back();
                s.keys.pop_back();
                if (!s.leaf()) {
                    c.children.insert(c.children.begin(), std::move(s.children.back()));
                    s.children.pop_back();
                }
            } else if (i < keys.size() && children[i + 1]->keys.size() >= t) {
                auto &c = *children[i], &s = *children[i + 1];
                c.keys.push_back(keys[i]);
                keys[i] = s.keys.front();
                s.keys.erase(s.keys.begin());
                if (!s.leaf()) {
                    c.children.push_back(std::move(s.children.front()));
                    s.children.erase(s.children.begin());
                }
            } else if (i < keys.size())
                merge(i);
            else {
                --i;
                merge(i);
            }
            return i;
        }
        int min() const {
            return leaf() ? keys.front() : children.front()->min();
        }
        int max() const {
            return leaf() ? keys.back() : children.back()->max();
        }
        void remove(int key, std::size_t t) {
            auto i = lower(key);
            if (i < keys.size() && keys[i] == key) {
                if (leaf())
                    keys.erase(keys.begin() + i);
                else if (children[i]->keys.size() >= t) {
                    int k = children[i]->max();
                    keys[i] = k;
                    children[i]->remove(k, t);
                } else if (children[i + 1]->keys.size() >= t) {
                    int k = children[i + 1]->min();
                    keys[i] = k;
                    children[i + 1]->remove(k, t);
                } else {
                    merge(i);
                    children[i]->remove(key, t);
                }
            } else if (!leaf()) {
                i = prepare(i, t);
                children[i]->remove(key, t);
            }
        }
    };
    Node root_;
    std::size_t degree_, size_ = 0;
    friend struct BTreeTest;

  public:
    explicit BTree(std::size_t degree = 2) : degree_(degree) {
        if (degree < 2 || degree > std::numeric_limits<std::size_t>::max() / 2)
            throw std::invalid_argument("invalid B-tree degree");
    }
    bool contains(int key) const {
        return root_.contains(key);
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
        if (root_.keys.size() == 2 * degree_ - 1) {
            auto old = std::make_unique<Node>(std::move(root_));
            root_ = Node{};
            root_.children.push_back(std::move(old));
            root_.split(0, degree_);
        }
        root_.insert_nonfull(key, degree_);
        ++size_;
        return true;
    }
    bool remove(int key) {
        if (!contains(key))
            return false;
        root_.remove(key, degree_);
        if (root_.keys.empty() && !root_.leaf()) {
            auto child = std::move(root_.children[0]);
            root_ = std::move(*child);
        }
        --size_;
        return true;
    }
};
// A 2–3–4 tree is the minimum-degree-2 B-tree.
class Tree234 : public BTree {
  public:
    Tree234() : BTree(2) {}
};
