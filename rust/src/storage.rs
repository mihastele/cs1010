//! Persistent, single-table storage for the database. Integer primary keys are
//! indexed by the B+ tree; values are kept in memory and rebuilt from the log.
//! Records are append-only: a failed or torn append is reported during reopen.
use crate::bplus::BPlusTree;
use std::collections::HashMap;
use std::fs::{File, OpenOptions};
use std::io::{self, Read, Write};
use std::path::Path;

const MAGIC: &[u8; 8] = b"CSDB\x01\0\0\0";
const MAX_VALUE: usize = 16 * 1024 * 1024;

/// A persistent set of `(integer primary key, byte value)` rows.
/// Each successful mutation is appended and synced before it is visible.
/// This educational format has no transactions, concurrent-writer locking,
/// checksums, compaction, or SQLite file compatibility.
pub struct Table {
    file: File,
    index: BPlusTree,
    values: HashMap<i32, Vec<u8>>,
    failed: bool,
}
impl Table {
    pub fn open(path: impl AsRef<Path>) -> io::Result<Self> {
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .open(path)?;
        let len = file.metadata()?.len();
        if len == 0 {
            file.write_all(MAGIC)?;
            file.sync_all()?;
        } else {
            let mut header = [0; 8];
            file.read_exact(&mut header)?;
            if &header != MAGIC {
                return Err(io::Error::new(
                    io::ErrorKind::InvalidData,
                    "invalid database header",
                ));
            }
        }
        let mut table = Self {
            file,
            index: BPlusTree::new(8),
            values: HashMap::new(),
            failed: false,
        };
        loop {
            let mut kind = [0];
            match table.file.read(&mut kind)? {
                0 => break,
                1 => {}
                _ => unreachable!(),
            }
            let mut key = [0; 4];
            table.file.read_exact(&mut key)?;
            let key = i32::from_le_bytes(key);
            match kind[0] {
                1 => {
                    let mut len = [0; 4];
                    table.file.read_exact(&mut len)?;
                    let len = u32::from_le_bytes(len) as usize;
                    if len > MAX_VALUE {
                        return Err(io::Error::new(
                            io::ErrorKind::InvalidData,
                            "value exceeds size limit",
                        ));
                    }
                    let mut value = vec![0; len];
                    table.file.read_exact(&mut value)?;
                    table.index.insert(key);
                    table.values.insert(key, value);
                }
                2 => {
                    table.index.remove(key);
                    table.values.remove(&key);
                }
                _ => {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidData,
                        "invalid record type",
                    ))
                }
            }
        }
        Ok(table)
    }
    fn append(&mut self, record: &[u8]) -> io::Result<()> {
        if self.failed {
            return Err(io::Error::other(
                "table cannot be used after a write failure; reopen it",
            ));
        }
        if let Err(err) = self
            .file
            .write_all(record)
            .and_then(|()| self.file.sync_all())
        {
            self.failed = true;
            return Err(err);
        }
        Ok(())
    }
    /// Insert a new key. Returns false if it already exists.
    pub fn insert(&mut self, key: i32, value: &[u8]) -> io::Result<bool> {
        if self.index.contains(key) {
            return Ok(false);
        }
        if value.len() > MAX_VALUE {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "value exceeds size limit",
            ));
        }
        let mut record = Vec::with_capacity(9 + value.len());
        record.push(1);
        record.extend_from_slice(&key.to_le_bytes());
        record.extend_from_slice(&(value.len() as u32).to_le_bytes());
        record.extend_from_slice(value);
        self.append(&record)?;
        self.index.insert(key);
        self.values.insert(key, value.to_vec());
        Ok(true)
    }
    pub fn get(&self, key: i32) -> Option<&[u8]> {
        self.values.get(&key).map(Vec::as_slice)
    }
    /// Delete a key. Returns false if it was absent.
    pub fn delete(&mut self, key: i32) -> io::Result<bool> {
        if !self.index.contains(key) {
            return Ok(false);
        }
        let mut record = vec![2];
        record.extend_from_slice(&key.to_le_bytes());
        self.append(&record)?;
        self.index.remove(key);
        self.values.remove(&key);
        Ok(true)
    }
    /// Read rows in ascending primary-key order, including both bounds.
    pub fn range(&self, first: i32, last: i32) -> Vec<(i32, &[u8])> {
        self.index
            .range(first, last)
            .into_iter()
            .map(|key| (key, self.values[&key].as_slice()))
            .collect()
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};
    static NEXT: AtomicU64 = AtomicU64::new(0);
    fn path() -> std::path::PathBuf {
        std::env::temp_dir().join(format!(
            "cs1010-db-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ))
    }
    #[test]
    fn persistence_and_indexed_ranges() {
        let path = path();
        {
            let mut t = Table::open(&path).unwrap();
            for k in (0..100).rev() {
                assert!(t.insert(k, format!("row{k}").as_bytes()).unwrap());
            }
            assert!(!t.insert(5, b"duplicate").unwrap());
            assert!(t.delete(5).unwrap());
            assert!(!t.delete(5).unwrap());
            assert!(t.insert(5, b"replacement").unwrap());
            assert!(t.delete(6).unwrap());
        }
        {
            let t = Table::open(&path).unwrap();
            assert_eq!(t.get(5), Some(&b"replacement"[..]));
            assert_eq!(t.get(6), None);
            let rows = t.range(3, 8);
            assert_eq!(
                rows.iter().map(|r| r.0).collect::<Vec<_>>(),
                vec![3, 4, 5, 7, 8]
            );
            assert!(t.range(8, 3).is_empty());
        }
        std::fs::remove_file(path).unwrap();
    }
    #[test]
    fn rejects_truncated_and_invalid_files() {
        let path = path();
        std::fs::write(&path, b"bad header").unwrap();
        assert!(Table::open(&path).is_err());
        std::fs::write(&path, [MAGIC.as_slice(), &[1, 0, 0]].concat()).unwrap();
        assert!(Table::open(&path).is_err());
        std::fs::remove_file(path).unwrap();
    }
}
