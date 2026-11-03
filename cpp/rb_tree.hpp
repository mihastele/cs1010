#pragma once
#include <memory>

// A left-leaning red-black tree: red links lean left, no red node has a
// red child, and all paths to an empty child have equal black height.
class RedBlackTree {
    struct Node {
        int key; bool red = true;
        std::unique_ptr<Node> left, right;
        explicit Node(int k) : key(k) {}
    };
    using Link = std::unique_ptr<Node>;
    Link root;
    static bool red(const Link& n) { return n && n->red; }
    static Link rotate_left(Link h) {
        auto x=std::move(h->right); h->right=std::move(x->left);
        x->red=h->red; h->red=true; x->left=std::move(h); return x;
    }
    static Link rotate_right(Link h) {
        auto x=std::move(h->left); h->left=std::move(x->right);
        x->red=h->red; h->red=true; x->right=std::move(h); return x;
    }
    static void put(Link& h, int key) {
        if (!h) { h=std::make_unique<Node>(key); return; }
        if (key<h->key) put(h->left,key);
        else if (key>h->key) put(h->right,key);
        if (red(h->right) && !red(h->left)) h=rotate_left(std::move(h));
        if (red(h->left) && red(h->left->left)) h=rotate_right(std::move(h));
        if (red(h->left) && red(h->right)) { h->red=!h->red; h->left->red=false; h->right->red=false; }
    }
    static void flip(Link& h) {
        h->red=!h->red; if(h->left) h->left->red=!h->left->red; if(h->right) h->right->red=!h->right->red;
    }
    static Link move_left(Link h) {
        flip(h); if(h->right && red(h->right->left)) { h->right=rotate_right(std::move(h->right)); h=rotate_left(std::move(h)); flip(h); } return h;
    }
    static Link move_right(Link h) {
        flip(h); if(h->left && red(h->left->left)) { h=rotate_right(std::move(h)); flip(h); } return h;
    }
    static Link balance(Link h) {
        if(red(h->right)) h=rotate_left(std::move(h));
        if(red(h->left) && red(h->left->left)) h=rotate_right(std::move(h));
        if(red(h->left) && red(h->right)) flip(h);
        return h;
    }
    static Link delete_min(Link h) {
        if(!h->left) return nullptr;
        if(!red(h->left) && !red(h->left->left)) h=move_left(std::move(h));
        h->left=delete_min(std::move(h->left)); return balance(std::move(h));
    }
    static Link delete_key(Link h, int key) {
        if(key<h->key) {
            if(!red(h->left) && !red(h->left->left)) h=move_left(std::move(h));
            h->left=delete_key(std::move(h->left),key);
        } else {
            if(red(h->left)) h=rotate_right(std::move(h));
            if(key==h->key && !h->right) return nullptr;
            if(!red(h->right) && !red(h->right->left)) h=move_right(std::move(h));
            if(key==h->key) { auto n=h->right.get(); while(n->left) n=n->left.get(); h->key=n->key; h->right=delete_min(std::move(h->right)); }
            else h->right=delete_key(std::move(h->right),key);
        }
        return balance(std::move(h));
    }
    friend struct TreeTest;
public:
    void insert(int key) { put(root,key); root->red=false; }
    bool remove(int key) {
        if(!contains(key)) return false;
        if(!red(root->left) && !red(root->right)) root->red=true;
        root=delete_key(std::move(root),key); if(root) root->red=false; return true;
    }
    bool contains(int key) const {
        auto n=root.get(); while(n) { if(key==n->key) return true; n=key<n->key?n->left.get():n->right.get(); } return false;
    }
};
