//! B-tree integer set. The minimum degree controls node capacity: except for
//! the root, nodes contain t-1..=2t-1 keys and all leaves have equal depth.
#[derive(Debug, Default)]
struct Node {
    keys: Vec<i32>,
    children: Vec<Node>,
}
impl Node {
    fn leaf(&self) -> bool {
        self.children.is_empty()
    }
    fn contains(&self, key: i32) -> bool {
        match self.keys.binary_search(&key) {
            Ok(_) => true,
            Err(i) => !self.leaf() && self.children[i].contains(key),
        }
    }
    // Split a full child and promote its middle key into this node.
    // The median moves into the parent, leaving at least t-1 keys on each side:
    //        [p]                        [m | p]
    //       /   \          ->           /   |   \
    //  [a ... m ... z]               [a...] [...z]
    fn split(&mut self, i: usize, t: usize) {
        let child = &mut self.children[i];
        let right_keys = child.keys.split_off(t);
        let median = child.keys.pop().unwrap();
        let right_children = if child.leaf() {
            Vec::new()
        } else {
            child.children.split_off(t)
        };
        self.keys.insert(i, median);
        self.children.insert(
            i + 1,
            Node {
                keys: right_keys,
                children: right_children,
            },
        );
    }
    fn insert_nonfull(&mut self, key: i32, t: usize) {
        let mut i = self.keys.binary_search(&key).unwrap_err();
        if self.leaf() {
            self.keys.insert(i, key);
            return;
        }
        if self.children[i].keys.len() == 2 * t - 1 {
            self.split(i, t);
            if key > self.keys[i] {
                i += 1;
            }
        }
        self.children[i].insert_nonfull(key, t);
    }
    // Pull the separator down between siblings so deletion can continue
    // in a single node without violating minimum occupancy.
    fn merge(&mut self, i: usize) {
        let separator = self.keys.remove(i);
        let right = self.children.remove(i + 1);
        let left = &mut self.children[i];
        left.keys.push(separator);
        left.keys.extend(right.keys);
        left.children.extend(right.children);
    }
    // Before descending, ensure the child has at least t keys by borrowing
    // from a sibling or merging; this prevents an underflow on the way back.
    fn prepare(&mut self, mut i: usize, t: usize) -> usize {
        if self.children[i].keys.len() >= t {
            return i;
        }
        if i > 0 && self.children[i - 1].keys.len() >= t {
            let (left, right) = self.children.split_at_mut(i);
            let sibling = &mut left[i - 1];
            let child = &mut right[0];
            child.keys.insert(0, self.keys[i - 1]);
            self.keys[i - 1] = sibling.keys.pop().unwrap();
            if !sibling.leaf() {
                child.children.insert(0, sibling.children.pop().unwrap());
            }
        } else if i < self.keys.len() && self.children[i + 1].keys.len() >= t {
            let (left, right) = self.children.split_at_mut(i + 1);
            let child = &mut left[i];
            let sibling = &mut right[0];
            child.keys.push(self.keys[i]);
            self.keys[i] = sibling.keys.remove(0);
            if !sibling.leaf() {
                child.children.push(sibling.children.remove(0));
            }
        } else if i < self.keys.len() {
            self.merge(i);
        } else {
            i -= 1;
            self.merge(i);
        }
        i
    }
    fn min(&self) -> i32 {
        if self.leaf() {
            self.keys[0]
        } else {
            self.children[0].min()
        }
    }
    fn max(&self) -> i32 {
        if self.leaf() {
            *self.keys.last().unwrap()
        } else {
            self.children.last().unwrap().max()
        }
    }
    fn remove(&mut self, key: i32, t: usize) {
        match self.keys.binary_search(&key) {
            Ok(i) if self.leaf() => {
                self.keys.remove(i);
            }
            Ok(i) if self.children[i].keys.len() >= t => {
                let predecessor = self.children[i].max();
                self.keys[i] = predecessor;
                self.children[i].remove(predecessor, t);
            }
            Ok(i) if self.children[i + 1].keys.len() >= t => {
                let successor = self.children[i + 1].min();
                self.keys[i] = successor;
                self.children[i + 1].remove(successor, t);
            }
            Ok(i) => {
                self.merge(i);
                self.children[i].remove(key, t);
            }
            Err(i) if !self.leaf() => {
                let i = self.prepare(i, t);
                self.children[i].remove(key, t);
            }
            Err(_) => {}
        }
    }
}
/// An ordered set of unique integer keys backed by a B-tree.
#[derive(Debug)]
pub struct BTree {
    root: Node,
    degree: usize,
    len: usize,
}
impl BTree {
    /// Creates a tree with minimum degree at least 2.
    pub fn new(degree: usize) -> Self {
        assert!(degree >= 2 && degree <= usize::MAX / 2);
        Self {
            root: Node::default(),
            degree,
            len: 0,
        }
    }
    pub fn len(&self) -> usize {
        self.len
    }
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
    pub fn contains(&self, key: i32) -> bool {
        self.root.contains(key)
    }
    /// Returns false for a duplicate.
    pub fn insert(&mut self, key: i32) -> bool {
        if self.contains(key) {
            return false;
        }
        let t = self.degree;
        if self.root.keys.len() == 2 * t - 1 {
            let old = std::mem::take(&mut self.root);
            self.root.children.push(old);
            self.root.split(0, t);
        }
        self.root.insert_nonfull(key, t);
        self.len += 1;
        true
    }
    /// Returns false when the key is absent.
    pub fn remove(&mut self, key: i32) -> bool {
        if !self.contains(key) {
            return false;
        }
        self.root.remove(key, self.degree);
        if self.root.keys.is_empty() && !self.root.leaf() {
            self.root = self.root.children.remove(0);
        }
        self.len -= 1;
        true
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeSet;
    fn check(
        n: &Node,
        t: usize,
        root: bool,
        low: i64,
        high: i64,
        depth: usize,
        leaf_depth: &mut Option<usize>,
    ) -> usize {
        assert!(n.keys.len() <= 2 * t - 1);
        if !root {
            assert!(n.keys.len() >= t - 1);
        }
        if root && !n.leaf() {
            assert!(!n.keys.is_empty());
        }
        let mut prev = low;
        for &k in &n.keys {
            assert!((k as i64) > prev && (k as i64) < high);
            prev = k as i64;
        }
        if n.leaf() {
            assert_eq!(*leaf_depth.get_or_insert(depth), depth);
            return n.keys.len();
        }
        assert_eq!(n.children.len(), n.keys.len() + 1);
        let mut total = n.keys.len();
        for (i, child) in n.children.iter().enumerate() {
            let l = if i == 0 { low } else { n.keys[i - 1] as i64 };
            let h = if i == n.keys.len() {
                high
            } else {
                n.keys[i] as i64
            };
            total += check(child, t, false, l, h, depth + 1, leaf_depth);
        }
        total
    }
    #[test]
    fn mixed_operations() {
        for degree in [2, 3, 5, 16] {
            let mut tree = BTree::new(degree);
            let mut reference = BTreeSet::new();
            let mut state = 42u32;
            for _ in 0..12000 {
                state = state.wrapping_mul(1664525).wrapping_add(1013904223);
                let key = ((state >> 8) % 300) as i32 - 150;
                if state & 1 == 0 {
                    assert_eq!(tree.insert(key), reference.insert(key));
                } else {
                    assert_eq!(tree.remove(key), reference.remove(&key));
                }
                assert_eq!(tree.len(), reference.len());
                assert_eq!(
                    check(&tree.root, degree, true, i64::MIN, i64::MAX, 0, &mut None),
                    tree.len()
                );
                for k in -150..150 {
                    assert_eq!(tree.contains(k), reference.contains(&k));
                }
            }
            for k in reference {
                assert!(tree.remove(k));
                check(&tree.root, degree, true, i64::MIN, i64::MAX, 0, &mut None);
            }
            assert!(tree.is_empty());
        }
    }
    #[test]
    fn boundaries() {
        let mut tree = BTree::new(2);
        for k in [i32::MIN, 0, i32::MAX] {
            assert!(tree.insert(k));
            assert!(!tree.insert(k));
        }
        for k in [i32::MIN, 0, i32::MAX] {
            assert!(tree.remove(k));
            assert!(!tree.remove(k));
        }
    }
}
