//! A 2â€“3 tree is represented by a left-leaning red-black tree. A black node
//! and its red left child encode a 3-node; an unpaired black node encodes a
//! 2-node. Red-black rotations and color changes implement splits and merges.
use crate::RedBlackTree;

/// An ordered integer set backed by a 2â€“3 tree encoded using binary links.
#[derive(Debug, Default)]
pub struct Tree23 {
    tree: RedBlackTree,
    len: usize,
}
impl Tree23 {
    pub fn new() -> Self {
        Self::default()
    }
    pub fn len(&self) -> usize {
        self.len
    }
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
    pub fn contains(&self, key: i32) -> bool {
        self.tree.contains(key)
    }
    /// Insert a key, returning false if it was already present.
    pub fn insert(&mut self, key: i32) -> bool {
        if self.contains(key) {
            return false;
        }
        self.tree.insert(key);
        self.len += 1;
        true
    }
    /// Remove a key, returning false if it was absent.
    pub fn remove(&mut self, key: i32) -> bool {
        if !self.tree.remove(key) {
            return false;
        }
        self.len -= 1;
        true
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeSet;
    #[test]
    fn mixed_operations_and_decoded_invariants() {
        let mut tree = Tree23::new();
        let mut expected = BTreeSet::new();
        let mut state = 42u32;
        for _ in 0..12000 {
            state = state.wrapping_mul(1664525).wrapping_add(1013904223);
            let key = ((state >> 8) % 300) as i32 - 150;
            if state & 1 == 0 {
                assert_eq!(tree.insert(key), expected.insert(key));
            } else {
                assert_eq!(tree.remove(key), expected.remove(&key));
            }
            assert_eq!(tree.len(), expected.len());
            assert_eq!(crate::red_black_tree::check_23(&tree.tree), tree.len());
            for k in -150..150 {
                assert_eq!(tree.contains(k), expected.contains(&k));
            }
        }
        for key in expected {
            assert!(tree.remove(key));
            crate::red_black_tree::check_23(&tree.tree);
        }
        assert!(tree.is_empty());
    }
}
