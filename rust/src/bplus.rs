//! An in-memory B+ tree integer set. Keys live in leaves; internal keys are
//! copies of the smallest key in each child after the first. Leaves form a
//! linked list for ordered scans. Arena indexes keep those links safe in Rust.
#[derive(Debug)]
struct Node {
    keys: Vec<i32>,
    children: Vec<usize>,
    next: Option<usize>,
}
impl Node {
    fn leaf() -> Self {
        Self {
            keys: Vec::new(),
            children: Vec::new(),
            next: None,
        }
    }
    fn internal(children: Vec<usize>) -> Self {
        Self {
            keys: Vec::new(),
            children,
            next: None,
        }
    }
    fn is_leaf(&self) -> bool {
        self.children.is_empty()
    }
}
/// An ordered set with at most `2 * degree - 1` keys per node.
#[derive(Debug)]
pub struct BPlusTree {
    nodes: Vec<Option<Node>>,
    free: Vec<usize>,
    root: usize,
    degree: usize,
    len: usize,
}
impl BPlusTree {
    pub fn new(degree: usize) -> Self {
        assert!(degree >= 2 && degree <= usize::MAX / 2);
        Self {
            nodes: vec![Some(Node::leaf())],
            free: Vec::new(),
            root: 0,
            degree,
            len: 0,
        }
    }
    fn node(&self, id: usize) -> &Node {
        self.nodes[id].as_ref().unwrap()
    }
    fn node_mut(&mut self, id: usize) -> &mut Node {
        self.nodes[id].as_mut().unwrap()
    }
    fn alloc(&mut self, node: Node) -> usize {
        if let Some(id) = self.free.pop() {
            self.nodes[id] = Some(node);
            id
        } else {
            self.nodes.push(Some(node));
            self.nodes.len() - 1
        }
    }
    fn release(&mut self, id: usize) -> Node {
        let node = self.nodes[id].take().unwrap();
        self.free.push(id);
        node
    }
    fn minimum(&self, mut id: usize) -> i32 {
        while !self.node(id).is_leaf() {
            id = self.node(id).children[0];
        }
        self.node(id).keys[0]
    }
    // Internal separators are derived, not independent copies of data.
    fn refresh(&mut self, id: usize) {
        let children = self.node(id).children.clone();
        if !children.is_empty() {
            let keys = children.iter().skip(1).map(|&c| self.minimum(c)).collect();
            self.node_mut(id).keys = keys;
        }
    }
    fn leaf_for(&self, key: i32) -> usize {
        let mut id = self.root;
        while !self.node(id).is_leaf() {
            let n = self.node(id);
            let i = n.keys.partition_point(|&k| k <= key);
            id = n.children[i];
        }
        id
    }
    pub fn len(&self) -> usize {
        self.len
    }
    pub fn is_empty(&self) -> bool {
        self.len == 0
    }
    pub fn contains(&self, key: i32) -> bool {
        self.node(self.leaf_for(key))
            .keys
            .binary_search(&key)
            .is_ok()
    }
    /// Returns false for a duplicate key.
    pub fn insert(&mut self, key: i32) -> bool {
        if self.contains(key) {
            return false;
        }
        if let Some(right) = self.insert_node(self.root, key) {
            let root = self.alloc(Node::internal(vec![self.root, right]));
            self.refresh(root);
            self.root = root;
        }
        self.len += 1;
        true
    }
    // Return the new right sibling if this node overflows.
    // A leaf split retains all data keys in leaves, unlike a B-tree split:
    //   [a b c d] -> [a b] -> [c d]     parent separator: c
    // The parent stores a routing copy of the right leaf's first key.
    fn insert_node(&mut self, id: usize, key: i32) -> Option<usize> {
        if self.node(id).is_leaf() {
            let i = self.node(id).keys.binary_search(&key).unwrap_err();
            self.node_mut(id).keys.insert(i, key);
            if self.node(id).keys.len() < 2 * self.degree {
                return None;
            }
            let t = self.degree;
            let keys = self.node_mut(id).keys.split_off(t);
            let next = self.node(id).next;
            let right = self.alloc(Node {
                keys,
                children: Vec::new(),
                next,
            });
            self.node_mut(id).next = Some(right);
            Some(right)
        } else {
            let i = self.node(id).keys.partition_point(|&k| k <= key);
            let child = self.node(id).children[i];
            if let Some(right) = self.insert_node(child, key) {
                self.node_mut(id).children.insert(i + 1, right);
            }
            self.refresh(id);
            if self.node(id).children.len() <= 2 * self.degree {
                return None;
            }
            let t = self.degree;
            let children = self.node_mut(id).children.split_off(t);
            self.refresh(id);
            let right = self.alloc(Node::internal(children));
            self.refresh(right);
            Some(right)
        }
    }
    fn underfull(&self, id: usize) -> bool {
        let n = self.node(id);
        if n.is_leaf() {
            n.keys.len() < self.degree - 1
        } else {
            n.children.len() < self.degree
        }
    }
    fn can_lend(&self, id: usize) -> bool {
        let n = self.node(id);
        if n.is_leaf() {
            n.keys.len() > self.degree - 1
        } else {
            n.children.len() > self.degree
        }
    }
    // Leaves borrow keys; internal nodes borrow child pointers. A merge
    // reconnects the leaf chain, and the caller refreshes routing separators.
    fn repair(&mut self, parent: usize, i: usize) {
        let child = self.node(parent).children[i];
        if !self.underfull(child) {
            return;
        }
        if i > 0 && self.can_lend(self.node(parent).children[i - 1]) {
            let left = self.node(parent).children[i - 1];
            if self.node(child).is_leaf() {
                let k = self.node_mut(left).keys.pop().unwrap();
                self.node_mut(child).keys.insert(0, k);
            } else {
                let c = self.node_mut(left).children.pop().unwrap();
                self.node_mut(child).children.insert(0, c);
                self.refresh(left);
                self.refresh(child);
            }
        } else if i + 1 < self.node(parent).children.len()
            && self.can_lend(self.node(parent).children[i + 1])
        {
            let right = self.node(parent).children[i + 1];
            if self.node(child).is_leaf() {
                let k = self.node_mut(right).keys.remove(0);
                self.node_mut(child).keys.push(k);
            } else {
                let c = self.node_mut(right).children.remove(0);
                self.node_mut(child).children.push(c);
                self.refresh(right);
                self.refresh(child);
            }
        } else {
            let j = if i > 0 { i - 1 } else { i };
            let left = self.node(parent).children[j];
            let right = self.node(parent).children[j + 1];
            self.node_mut(parent).children.remove(j + 1);
            let mut other = self.release(right);
            if self.node(left).is_leaf() {
                self.node_mut(left).keys.append(&mut other.keys);
                self.node_mut(left).next = other.next;
            } else {
                self.node_mut(left).children.append(&mut other.children);
                self.refresh(left);
            }
        }
    }
    fn remove_node(&mut self, id: usize, key: i32) {
        if self.node(id).is_leaf() {
            let i = self.node(id).keys.binary_search(&key).unwrap();
            self.node_mut(id).keys.remove(i);
        } else {
            let i = self.node(id).keys.partition_point(|&k| k <= key);
            let child = self.node(id).children[i];
            self.remove_node(child, key);
            self.repair(id, i);
            self.refresh(id);
        }
    }
    /// Returns false when the key is absent.
    pub fn remove(&mut self, key: i32) -> bool {
        if !self.contains(key) {
            return false;
        }
        self.remove_node(self.root, key);
        self.len -= 1;
        if !self.node(self.root).is_leaf() && self.node(self.root).children.len() == 1 {
            let old = self.root;
            self.root = self.node(old).children[0];
            self.release(old);
        }
        true
    }
    /// Return keys in the inclusive range `start..=end`, in ascending order.
    pub fn range(&self, start: i32, end: i32) -> Vec<i32> {
        if start > end {
            return Vec::new();
        }
        let mut out = Vec::new();
        let mut id = Some(self.leaf_for(start));
        while let Some(i) = id {
            let n = self.node(i);
            for &key in &n.keys {
                if key > end {
                    return out;
                }
                if key >= start {
                    out.push(key);
                }
            }
            id = n.next;
        }
        out
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeSet;
    fn check(
        t: &BPlusTree,
        id: usize,
        root: bool,
        depth: usize,
        leaf_depth: &mut Option<usize>,
        leaves: &mut Vec<usize>,
    ) -> Vec<i32> {
        let n = t.node(id);
        assert!(n.keys.len() <= 2 * t.degree - 1);
        if n.is_leaf() {
            if !root {
                assert!(n.keys.len() >= t.degree - 1);
            }
            assert!(n.keys.windows(2).all(|w| w[0] < w[1]));
            assert_eq!(*leaf_depth.get_or_insert(depth), depth);
            leaves.push(id);
            return n.keys.clone();
        }
        if root {
            assert!(n.children.len() >= 2);
        } else {
            assert!(n.children.len() >= t.degree);
        }
        assert_eq!(n.keys.len() + 1, n.children.len());
        let mut all = Vec::new();
        for (i, &child) in n.children.iter().enumerate() {
            let keys = check(t, child, false, depth + 1, leaf_depth, leaves);
            assert!(!keys.is_empty());
            if i > 0 {
                assert_eq!(n.keys[i - 1], keys[0]);
                assert!(all.last().unwrap() < &keys[0]);
            }
            all.extend(keys);
        }
        all
    }
    fn validate(tree: &BPlusTree, expected: &BTreeSet<i32>) {
        let mut leaves = Vec::new();
        let keys = check(tree, tree.root, true, 0, &mut None, &mut leaves);
        assert_eq!(keys, expected.iter().copied().collect::<Vec<_>>());
        for (i, &id) in leaves.iter().enumerate() {
            assert_eq!(tree.node(id).next, leaves.get(i + 1).copied());
        }
        assert_eq!(tree.len(), expected.len());
    }
    #[test]
    fn mixed_operations() {
        for degree in [2, 3, 5, 16] {
            let mut t = BPlusTree::new(degree);
            let mut expected = BTreeSet::new();
            let mut state = 42u32;
            for _ in 0..12000 {
                state = state.wrapping_mul(1664525).wrapping_add(1013904223);
                let key = ((state >> 8) % 300) as i32 - 150;
                if state & 1 == 0 {
                    assert_eq!(t.insert(key), expected.insert(key));
                } else {
                    assert_eq!(t.remove(key), expected.remove(&key));
                }
                validate(&t, &expected);
                let a = key - 20;
                let b = key + 20;
                assert_eq!(
                    t.range(a, b),
                    expected.range(a..=b).copied().collect::<Vec<_>>()
                );
            }
            for key in expected.clone() {
                assert!(t.remove(key));
                expected.remove(&key);
                validate(&t, &expected);
            }
            assert!(t.is_empty());
        }
    }
    #[test]
    fn boundaries() {
        let mut t = BPlusTree::new(2);
        for key in [i32::MIN, 0, i32::MAX] {
            assert!(t.insert(key));
            assert!(!t.insert(key));
        }
        assert_eq!(t.range(i32::MIN, i32::MAX), vec![i32::MIN, 0, i32::MAX]);
        assert!(t.range(2, 1).is_empty());
        for key in [i32::MIN, 0, i32::MAX] {
            assert!(t.remove(key));
            assert!(!t.remove(key));
        }
    }
}
