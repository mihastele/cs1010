//! A left-leaning red-black tree of integer keys.
//! Red links lean left; no path contains consecutive red links; every path
//! from a node to an empty child contains the same number of black links.
#[derive(Debug)]
struct Node {
    key: i32,
    red: bool,
    left: Link,
    right: Link,
}
type Link = Option<Box<Node>>;

#[derive(Default, Debug)]
pub struct RedBlackTree {
    root: Link,
}

fn red(n: &Link) -> bool {
    n.as_ref().is_some_and(|n| n.red)
}
// A rotation preserves the sorted order A, h, B, x, C:
//        h                x
//       / \              / \
//      A   x     ->     h   C
//         / \          / \
//        B   C         A   B
fn rotate_left(mut h: Box<Node>) -> Box<Node> {
    let mut x = h.right.take().unwrap();
    h.right = x.left.take();
    x.red = h.red;
    h.red = true;
    x.left = Some(h);
    x
}
fn rotate_right(mut h: Box<Node>) -> Box<Node> {
    let mut x = h.left.take().unwrap();
    h.left = x.right.take();
    x.red = h.red;
    h.red = true;
    x.right = Some(h);
    x
}
fn insert(h: Link, key: i32) -> Box<Node> {
    let Some(mut h) = h else {
        return Box::new(Node {
            key,
            red: true,
            left: None,
            right: None,
        });
    };
    if key < h.key {
        h.left = Some(insert(h.left.take(), key));
    } else if key > h.key {
        h.right = Some(insert(h.right.take(), key));
    }
    if red(&h.right) && !red(&h.left) {
        h = rotate_left(h);
    }
    if red(&h.left) && red(&h.left.as_ref().unwrap().left) {
        h = rotate_right(h);
    }
    if red(&h.left) && red(&h.right) {
        h.red = !h.red;
        h.left.as_mut().unwrap().red = false;
        h.right.as_mut().unwrap().red = false;
    }
    h
}
// Temporarily merge 2-nodes on the descent so deletion never removes a
// black link without a red link available to replace it.
fn flip(h: &mut Box<Node>) {
    h.red = !h.red;
    if let Some(n) = h.left.as_mut() {
        n.red = !n.red;
    }
    if let Some(n) = h.right.as_mut() {
        n.red = !n.red;
    }
}
fn move_red_left(mut h: Box<Node>) -> Box<Node> {
    flip(&mut h);
    if h.right.as_ref().is_some_and(|n| red(&n.left)) {
        h.right = Some(rotate_right(h.right.take().unwrap()));
        h = rotate_left(h);
        flip(&mut h);
    }
    h
}
fn move_red_right(mut h: Box<Node>) -> Box<Node> {
    flip(&mut h);
    if h.left.as_ref().is_some_and(|n| red(&n.left)) {
        h = rotate_right(h);
        flip(&mut h);
    }
    h
}
// Restore left-leaning red links and split temporary 4-nodes on the way up.
fn balance(mut h: Box<Node>) -> Box<Node> {
    if red(&h.right) {
        h = rotate_left(h);
    }
    if red(&h.left) && red(&h.left.as_ref().unwrap().left) {
        h = rotate_right(h);
    }
    if red(&h.left) && red(&h.right) {
        flip(&mut h);
    }
    h
}
fn delete_min(mut h: Box<Node>) -> Link {
    h.left.as_ref()?;
    if !red(&h.left) && !red(&h.left.as_ref().unwrap().left) {
        h = move_red_left(h);
    }
    h.left = delete_min(h.left.take().unwrap());
    Some(balance(h))
}
fn delete(mut h: Box<Node>, key: i32) -> Link {
    if key < h.key {
        if !red(&h.left) && !red(&h.left.as_ref().unwrap().left) {
            h = move_red_left(h);
        }
        h.left = delete(h.left.take().unwrap(), key);
    } else {
        if red(&h.left) {
            h = rotate_right(h);
        }
        if key == h.key && h.right.is_none() {
            return None;
        }
        if !red(&h.right) && !red(&h.right.as_ref().unwrap().left) {
            h = move_red_right(h);
        }
        if key == h.key {
            let mut successor = h.right.as_ref().unwrap().as_ref();
            while let Some(left) = successor.left.as_ref() {
                successor = left;
            }
            h.key = successor.key;
            h.right = delete_min(h.right.take().unwrap());
        } else {
            h.right = delete(h.right.take().unwrap(), key);
        }
    }
    Some(balance(h))
}
impl RedBlackTree {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn insert(&mut self, key: i32) {
        self.root = Some(insert(self.root.take(), key));
        self.root.as_mut().unwrap().red = false;
    }
    /// Remove a key, returning whether it was present.
    pub fn remove(&mut self, key: i32) -> bool {
        if !self.contains(key) {
            return false;
        }
        let root = self.root.as_mut().unwrap();
        if !red(&root.left) && !red(&root.right) {
            root.red = true;
        }
        self.root = delete(self.root.take().unwrap(), key);
        if let Some(root) = self.root.as_mut() {
            root.red = false;
        }
        true
    }
    pub fn contains(&self, key: i32) -> bool {
        let mut n = self.root.as_deref();
        while let Some(node) = n {
            if key == node.key {
                return true;
            }
            n = if key < node.key {
                node.left.as_deref()
            } else {
                node.right.as_deref()
            };
        }
        false
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    fn check(n: &Link, min: i64, max: i64) -> usize {
        let Some(n) = n else {
            return 1;
        };
        assert!((n.key as i64) > min && (n.key as i64) < max);
        assert!(!red(&n.right));
        if n.red {
            assert!(!red(&n.left));
        }
        let l = check(&n.left, min, n.key as i64);
        let r = check(&n.right, n.key as i64, max);
        assert_eq!(l, r);
        l + usize::from(!n.red)
    }
    #[test]
    fn insertion_and_invariants() {
        for reverse in [false, true] {
            let mut t = RedBlackTree::new();
            assert!(!t.contains(0));
            for i in 0..1000 {
                let k = if reverse { 999 - i } else { i };
                t.insert(k);
                t.insert(k);
                assert!(!red(&t.root));
                check(&t.root, i64::MIN, i64::MAX);
            }
            for i in 0..1000 {
                assert!(t.contains(i));
            }
            assert!(!t.contains(-1));
            assert!(!t.contains(1000));
        }
    }
    #[test]
    fn mixed_operations_match_set() {
        use std::collections::BTreeSet;
        let mut tree = RedBlackTree::new();
        let mut expected = BTreeSet::new();
        let mut state = 42u32;
        for _ in 0..10000 {
            state = state.wrapping_mul(1664525).wrapping_add(1013904223);
            let key = ((state >> 8) % 200) as i32 - 100;
            if state & 1 == 0 {
                tree.insert(key);
                expected.insert(key);
            } else {
                assert_eq!(tree.remove(key), expected.remove(&key));
            }
            assert!(!red(&tree.root));
            check(&tree.root, i64::MIN, i64::MAX);
            for k in -100..100 {
                assert_eq!(tree.contains(k), expected.contains(&k));
            }
        }
        for key in expected {
            assert!(tree.remove(key));
            check(&tree.root, i64::MIN, i64::MAX);
        }
        assert!(tree.root.is_none());
    }
}

#[cfg(test)]
pub(crate) fn check_23(tree: &RedBlackTree) -> usize {
    check_23_node(&tree.root, i64::MIN, i64::MAX, 0, &mut None)
}

#[cfg(test)]
// Decode each black node and its optional red left link as one 2â€“3 node.
fn check_23_node(link: &Link, lo: i64, hi: i64, depth: usize, leaf: &mut Option<usize>) -> usize {
    let Some(n) = link else {
        assert_eq!(*leaf.get_or_insert(depth), depth);
        return 0;
    };
    assert!(!n.red && !red(&n.right));
    if red(&n.left) {
        let left = n.left.as_ref().unwrap();
        assert!(!red(&left.left) && !red(&left.right));
        assert!(lo < left.key as i64 && left.key < n.key && (n.key as i64) < hi);
        2 + check_23_node(&left.left, lo, left.key as i64, depth + 1, leaf)
            + check_23_node(&left.right, left.key as i64, n.key as i64, depth + 1, leaf)
            + check_23_node(&n.right, n.key as i64, hi, depth + 1, leaf)
    } else {
        assert!(lo < n.key as i64 && (n.key as i64) < hi);
        1 + check_23_node(&n.left, lo, n.key as i64, depth + 1, leaf)
            + check_23_node(&n.right, n.key as i64, hi, depth + 1, leaf)
    }
}
