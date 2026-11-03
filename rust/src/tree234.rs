//! A 2–3–4 tree is a B-tree of minimum degree 2: non-root nodes have
//! one to three keys and internal nodes have two to four children.
use crate::btree::BTree;

#[derive(Debug)]
pub struct Tree234(BTree);
impl Default for Tree234 {
    fn default() -> Self {
        Self::new()
    }
}
impl Tree234 {
    pub fn new() -> Self {
        Self(BTree::new(2))
    }
    pub fn len(&self) -> usize {
        self.0.len()
    }
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
    pub fn contains(&self, key: i32) -> bool {
        self.0.contains(key)
    }
    pub fn insert(&mut self, key: i32) -> bool {
        self.0.insert(key)
    }
    pub fn remove(&mut self, key: i32) -> bool {
        self.0.remove(key)
    }
}
#[cfg(test)]
mod tests {
    use super::Tree234;
    #[test]
    fn set_operations() {
        let mut tree = Tree234::new();
        for key in 0..100 {
            assert!(tree.insert(key));
            assert!(!tree.insert(key));
        }
        for key in (0..100).rev() {
            assert!(tree.contains(key));
            assert!(tree.remove(key));
            assert!(!tree.remove(key));
        }
        assert!(tree.is_empty());
    }
}
