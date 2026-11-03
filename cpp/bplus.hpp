#pragma once
#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

// Keys live in linked leaves. Each separator is the minimum of the child
// immediately to its right. Nodes have at most 2*degree-1 keys.
class BPlusTree {
    struct Node {
        std::vector<int> keys;
        std::vector<std::unique_ptr<Node>> children;
        Node* next=nullptr; // Non-owning; maintained when leaves split or merge.
        bool leaf() const { return children.empty(); }
    };
    std::unique_ptr<Node> root_=std::make_unique<Node>();
    std::size_t degree_, size_=0;
    friend struct BPlusTest;
    static int minimum(const Node* node) {
        while (!node->leaf()) node=node->children.front().get();
        return node->keys.front();
    }
    static void refresh(Node* node) {
        if (node->leaf()) return;
        node->keys.clear();
        for (std::size_t i=1;i<node->children.size();++i) node->keys.push_back(minimum(node->children[i].get()));
    }
    static std::size_t child_index(const Node* node,int key) {
        return std::upper_bound(node->keys.begin(),node->keys.end(),key)-node->keys.begin();
    }
    const Node* leaf_for(int key) const {
        auto node=root_.get();
        while (!node->leaf()) node=node->children[child_index(node,key)].get();
        return node;
    }
    // Returns a new right sibling when the node overflows.
    std::unique_ptr<Node> insert_node(Node* node,int key) {
        if (node->leaf()) {
            node->keys.insert(std::lower_bound(node->keys.begin(),node->keys.end(),key),key);
            if (node->keys.size()<2*degree_) return nullptr;
            auto right=std::make_unique<Node>();
            right->keys.assign(node->keys.begin()+degree_,node->keys.end());
            node->keys.resize(degree_); right->next=node->next; node->next=right.get();
            return right;
        }
        auto i=child_index(node,key);
        auto right=insert_node(node->children[i].get(),key);
        if (right) node->children.insert(node->children.begin()+i+1,std::move(right));
        refresh(node);
        if (node->children.size()<=2*degree_) return nullptr;
        auto sibling=std::make_unique<Node>();
        for (std::size_t j=degree_;j<node->children.size();++j) sibling->children.push_back(std::move(node->children[j]));
        node->children.resize(degree_); refresh(node); refresh(sibling.get());
        return sibling;
    }
    bool underfull(const Node* n) const { return n->leaf()?n->keys.size()<degree_-1:n->children.size()<degree_; }
    bool can_lend(const Node* n) const { return n->leaf()?n->keys.size()>degree_-1:n->children.size()>degree_; }
    void repair(Node* parent,std::size_t i) {
        Node* child=parent->children[i].get();
        if (!underfull(child)) return;
        if (i>0 && can_lend(parent->children[i-1].get())) {
            Node* left=parent->children[i-1].get();
            if (child->leaf()) { child->keys.insert(child->keys.begin(),left->keys.back()); left->keys.pop_back(); }
            else { child->children.insert(child->children.begin(),std::move(left->children.back())); left->children.pop_back(); refresh(left); refresh(child); }
        } else if (i+1<parent->children.size() && can_lend(parent->children[i+1].get())) {
            Node* right=parent->children[i+1].get();
            if (child->leaf()) { child->keys.push_back(right->keys.front()); right->keys.erase(right->keys.begin()); }
            else { child->children.push_back(std::move(right->children.front())); right->children.erase(right->children.begin()); refresh(right); refresh(child); }
        } else {
            auto j=i>0?i-1:i;
            Node* left=parent->children[j].get();
            auto right=std::move(parent->children[j+1]);
            parent->children.erase(parent->children.begin()+j+1);
            if (left->leaf()) { left->keys.insert(left->keys.end(),right->keys.begin(),right->keys.end()); left->next=right->next; }
            else { for (auto &c:right->children) left->children.push_back(std::move(c)); refresh(left); }
        }
    }
    void remove_node(Node* node,int key) {
        if (node->leaf()) { node->keys.erase(std::lower_bound(node->keys.begin(),node->keys.end(),key)); return; }
        auto i=child_index(node,key); remove_node(node->children[i].get(),key); repair(node,i); refresh(node);
    }
public:
    explicit BPlusTree(std::size_t degree=2) : degree_(degree) {
        if (degree<2 || degree>std::numeric_limits<std::size_t>::max()/2) throw std::invalid_argument("invalid B+ tree degree");
    }
    std::size_t size() const { return size_; }
    bool empty() const { return size_==0; }
    bool contains(int key) const {
        auto leaf=leaf_for(key); return std::binary_search(leaf->keys.begin(),leaf->keys.end(),key);
    }
    bool insert(int key) {
        if (contains(key)) return false;
        auto right=insert_node(root_.get(),key);
        if (right) {
            auto root=std::make_unique<Node>(); root->children.push_back(std::move(root_));
            root->children.push_back(std::move(right)); refresh(root.get()); root_=std::move(root);
        }
        ++size_; return true;
    }
    bool remove(int key) {
        if (!contains(key)) return false;
        remove_node(root_.get(),key); --size_;
        if (!root_->leaf() && root_->children.size()==1) { auto old=std::move(root_); root_=std::move(old->children.front()); }
        return true;
    }
    std::vector<int> range(int first,int last) const {
        std::vector<int> result; if (first>last) return result;
        for (auto leaf=leaf_for(first);leaf;leaf=leaf->next)
            for (int key:leaf->keys) { if (key>last) return result; if (key>=first) result.push_back(key); }
        return result;
    }
};
