//! A page-backed B+ tree of unique i32 keys. Internal separators copy the
//! smallest key in each right child; all actual keys are stored in leaves.
//! This is an educational index: updates are not crash-atomic and no pages are
//! reclaimed yet. Do not open the same file with multiple writers.
use crate::pager::{PageId, Pager, PAGE_SIZE};
use std::io;
use std::path::Path;

// 127 keys and 128 child page numbers fit in a 4096-byte page.
const MAX_KEYS: usize = 127;
const SPLIT: usize = 64;
const HEADER: usize = 11; // type: u8, key count: u16, next leaf: u64
const LEAF: u8 = 1;
const INTERNAL: u8 = 2;
fn corrupt(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message)
}
#[derive(Debug)]
struct Node {
    keys: Vec<i32>,
    children: Vec<PageId>,
    next: Option<PageId>,
}
impl Node {
    fn is_leaf(&self) -> bool {
        self.children.is_empty()
    }
}
/// Persistent ordered set. `sync` flushes the file but does not provide atomicity.
pub struct DiskBPlusTree {
    pager: Pager,
}
impl DiskBPlusTree {
    pub fn open(path: impl AsRef<Path>) -> io::Result<Self> {
        let mut pager = Pager::open(path)?;
        if pager.root().is_none() {
            let root = pager.allocate()?;
            let mut page = [0; PAGE_SIZE];
            page[0] = LEAF;
            pager.write(root, &page)?;
            pager.set_root(Some(root))?;
            pager.sync()?;
        }
        Ok(Self { pager })
    }
    fn read(&mut self, id: PageId) -> io::Result<Node> {
        let page = self.pager.read(id)?;
        let count = u16::from_le_bytes([page[1], page[2]]) as usize;
        if count > MAX_KEYS {
            return Err(corrupt("invalid B+ tree key count"));
        }
        let leaf = match page[0] {
            LEAF => true,
            INTERNAL => false,
            _ => return Err(corrupt("invalid B+ tree page type")),
        };
        let raw_next = u64::from_le_bytes(page[3..11].try_into().unwrap());
        if !leaf && raw_next != 0 {
            return Err(corrupt("internal page has leaf link"));
        }
        let next = if raw_next == 0 {
            None
        } else {
            Some(
                self.pager
                    .page_id(raw_next)
                    .map_err(|_| corrupt("invalid leaf link"))?,
            )
        };
        let mut at = HEADER;
        let mut keys = Vec::with_capacity(count);
        for _ in 0..count {
            keys.push(i32::from_le_bytes(page[at..at + 4].try_into().unwrap()));
            at += 4;
        }
        if !keys.windows(2).all(|w| w[0] < w[1]) {
            return Err(corrupt("unsorted B+ tree keys"));
        }
        let mut children = Vec::new();
        if !leaf {
            children.reserve(count + 1);
            for _ in 0..=count {
                let raw = u64::from_le_bytes(page[at..at + 8].try_into().unwrap());
                at += 8;
                children.push(
                    self.pager
                        .page_id(raw)
                        .map_err(|_| corrupt("invalid child page"))?,
                );
            }
        }
        Ok(Node {
            keys,
            children,
            next,
        })
    }
    fn write(&mut self, id: PageId, node: &Node) -> io::Result<()> {
        if node.keys.len() > MAX_KEYS
            || (!node.is_leaf() && node.children.len() != node.keys.len() + 1)
        {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "invalid node shape",
            ));
        }
        let mut page = [0; PAGE_SIZE];
        page[0] = if node.is_leaf() { LEAF } else { INTERNAL };
        page[1..3].copy_from_slice(&(node.keys.len() as u16).to_le_bytes());
        page[3..11].copy_from_slice(&node.next.map_or(0, PageId::get).to_le_bytes());
        let mut at = HEADER;
        for key in &node.keys {
            page[at..at + 4].copy_from_slice(&key.to_le_bytes());
            at += 4;
        }
        for child in &node.children {
            page[at..at + 8].copy_from_slice(&child.get().to_le_bytes());
            at += 8;
        }
        self.pager.write(id, &page)
    }
    fn root(&self) -> PageId {
        self.pager.root().expect("tree has a root")
    }
    fn find_leaf(&mut self, key: i32) -> io::Result<(PageId, Node)> {
        let mut id = self.root();
        // Bound traversal so a corrupt cycle cannot loop forever.
        for _ in 0..self.pager.page_count() {
            let node = self.read(id)?;
            if node.is_leaf() {
                return Ok((id, node));
            }
            id = node.children[node.keys.partition_point(|&k| k <= key)];
        }
        Err(corrupt("cycle in B+ tree"))
    }
    pub fn contains(&mut self, key: i32) -> io::Result<bool> {
        Ok(self.find_leaf(key)?.1.keys.binary_search(&key).is_ok())
    }
    /// Insert a key, returning false for a duplicate. Call `sync` to flush.
    pub fn insert(&mut self, key: i32) -> io::Result<bool> {
        if self.contains(key)? {
            return Ok(false);
        }
        if let Some((separator, right)) = self.insert_node(self.root(), key, 0)? {
            let root = self.pager.allocate()?;
            self.write(
                root,
                &Node {
                    keys: vec![separator],
                    children: vec![self.root(), right],
                    next: None,
                },
            )?;
            self.pager.set_root(Some(root))?;
        }
        Ok(true)
    }
    // On overflow return the separator and the newly allocated right sibling.
    fn insert_node(
        &mut self,
        id: PageId,
        key: i32,
        depth: u64,
    ) -> io::Result<Option<(i32, PageId)>> {
        if depth >= self.pager.page_count() {
            return Err(corrupt("cycle in B+ tree"));
        }
        let mut node = self.read(id)?;
        if node.is_leaf() {
            let pos = node.keys.binary_search(&key).unwrap_err();
            node.keys.insert(pos, key);
            if node.keys.len() <= MAX_KEYS {
                self.write(id, &node)?;
                return Ok(None);
            }
            // The separator stays in the right leaf as a copy for its parent:
            // [a ...] -> [m ...],  parent adds m.
            let right_keys = node.keys.split_off(SPLIT);
            let separator = right_keys[0];
            let right = self.pager.allocate()?;
            self.write(
                right,
                &Node {
                    keys: right_keys,
                    children: vec![],
                    next: node.next,
                },
            )?;
            node.next = Some(right);
            self.write(id, &node)?;
            Ok(Some((separator, right)))
        } else {
            let pos = node.keys.partition_point(|&k| k <= key);
            if let Some((separator, right)) =
                self.insert_node(node.children[pos], key, depth + 1)?
            {
                node.keys.insert(pos, separator);
                node.children.insert(pos + 1, right);
            } else {
                return Ok(None);
            }
            if node.keys.len() <= MAX_KEYS {
                self.write(id, &node)?;
                return Ok(None);
            }
            let right_keys = node.keys.split_off(SPLIT + 1);
            let separator = node.keys.pop().unwrap();
            let right_children = node.children.split_off(SPLIT + 1);
            let right = self.pager.allocate()?;
            self.write(
                right,
                &Node {
                    keys: right_keys,
                    children: right_children,
                    next: None,
                },
            )?;
            self.write(id, &node)?;
            Ok(Some((separator, right)))
        }
    }
    /// Return all keys in the inclusive range, in order.
    pub fn range(&mut self, first: i32, last: i32) -> io::Result<Vec<i32>> {
        if first > last {
            return Ok(Vec::new());
        }
        let (_, mut node) = self.find_leaf(first)?;
        let mut result = Vec::new();
        for _ in 0..self.pager.page_count() {
            for &key in &node.keys {
                if key > last {
                    return Ok(result);
                }
                if key >= first {
                    result.push(key);
                }
            }
            match node.next {
                None => return Ok(result),
                Some(id) => {
                    node = self.read(id)?;
                    if !node.is_leaf() {
                        return Err(corrupt("leaf link points to internal page"));
                    }
                }
            }
        }
        Err(corrupt("cycle in leaf chain"))
    }
    pub fn sync(&mut self) -> io::Result<()> {
        self.pager.sync()
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeSet;
    use std::sync::atomic::{AtomicU64, Ordering};
    static NEXT: AtomicU64 = AtomicU64::new(0);
    fn path() -> std::path::PathBuf {
        std::env::temp_dir().join(format!(
            "cs1010-disk-tree-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ))
    }
    #[test]
    fn splits_reopen_and_scan() {
        let path = path();
        let mut expected = BTreeSet::new();
        {
            let mut tree = DiskBPlusTree::open(&path).unwrap();
            for i in 0..30000 {
                let key = (i * 7919) % 30000 - 15000;
                assert!(tree.insert(key).unwrap());
                expected.insert(key);
            }
            assert!(!tree.insert(0).unwrap());
            tree.insert(i32::MIN).unwrap();
            tree.insert(i32::MAX).unwrap();
            expected.insert(i32::MIN);
            expected.insert(i32::MAX);
            tree.sync().unwrap();
            let root = tree.read(tree.root()).unwrap();
            assert!(!root.is_leaf());
            assert!(!tree.read(root.children[0]).unwrap().is_leaf());
        }
        {
            let mut tree = DiskBPlusTree::open(&path).unwrap();
            assert_eq!(
                tree.range(i32::MIN, i32::MAX).unwrap(),
                expected.iter().copied().collect::<Vec<_>>()
            );
            for key in [-15001, -15000, 0, 14999, 15000, i32::MIN, i32::MAX] {
                assert_eq!(tree.contains(key).unwrap(), expected.contains(&key));
            }
            assert_eq!(tree.range(-3, 3).unwrap(), vec![-3, -2, -1, 0, 1, 2, 3]);
            assert!(tree.range(1, 0).unwrap().is_empty());
        }
        std::fs::remove_file(path).unwrap();
    }
}
