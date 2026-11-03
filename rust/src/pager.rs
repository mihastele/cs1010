//! Fixed-size page storage for the future on-disk B+ tree.
//!
//! Page 0 is a file header; pages 1 onward are available to the caller.
//! All page identifiers are stable offsets, not process-memory pointers.
//! This module does not yet provide transactions, journaling, free-page reuse,
//! checksums, locking, or crash-atomic updates across multiple pages.
use std::fs::{File, OpenOptions};
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::Path;

pub const PAGE_SIZE: usize = 4096;
const MAGIC: &[u8; 8] = b"CSPAGE\x01\0";
const ROOT_OFFSET: u64 = 8;

/// A nonzero page number. Page zero is reserved for the file header.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PageId(u64);
impl PageId {
    pub fn get(self) -> u64 {
        self.0
    }
}

/// A file containing fixed-size pages. Only one writer may use a file at once.
pub struct Pager {
    file: File,
    page_count: u64,
    root: Option<PageId>,
    failed: bool,
}
fn invalid_data(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, message)
}
fn invalid_input(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidInput, message)
}
impl Pager {
    /// Open an existing page file, or create a new empty one. Existing files
    /// must contain the header and an integral number of complete pages.
    pub fn open(path: impl AsRef<Path>) -> io::Result<Self> {
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(path)?;
        let mut length = file.metadata()?.len();
        if length == 0 {
            let mut header = [0u8; PAGE_SIZE];
            header[..MAGIC.len()].copy_from_slice(MAGIC);
            file.write_all(&header)?;
            file.sync_all()?;
            length = PAGE_SIZE as u64;
        }
        if length < PAGE_SIZE as u64 || length % PAGE_SIZE as u64 != 0 {
            return Err(invalid_data("incomplete page file"));
        }
        file.seek(SeekFrom::Start(0))?;
        let mut prefix = [0u8; 16];
        file.read_exact(&mut prefix)?;
        if &prefix[..8] != MAGIC {
            return Err(invalid_data("invalid page file header"));
        }
        let raw_root = u64::from_le_bytes(prefix[8..16].try_into().unwrap());
        let page_count = length / PAGE_SIZE as u64 - 1;
        if raw_root > page_count {
            return Err(invalid_data("root page is outside the file"));
        }
        Ok(Self {
            file,
            page_count,
            root: if raw_root == 0 {
                None
            } else {
                Some(PageId(raw_root))
            },
            failed: false,
        })
    }
    pub fn page_count(&self) -> u64 {
        self.page_count
    }
    pub fn root(&self) -> Option<PageId> {
        self.root
    }
    /// Convert a nonzero page number to an ID only if it is already allocated.
    pub fn page_id(&self, number: u64) -> io::Result<PageId> {
        if number == 0 || number > self.page_count {
            return Err(invalid_input("page is not allocated"));
        }
        Ok(PageId(number))
    }
    fn offset(&self, id: PageId) -> io::Result<u64> {
        self.page_id(id.0)?;
        id.0.checked_mul(PAGE_SIZE as u64)
            .ok_or_else(|| invalid_input("page offset overflow"))
    }
    fn ensure_writable(&self) -> io::Result<()> {
        if self.failed {
            Err(io::Error::other(
                "pager cannot be written after an I/O failure; reopen it",
            ))
        } else {
            Ok(())
        }
    }
    /// Allocate a zero-filled page at the end of the file. Call `sync` to
    /// request durability; a failed write can leave an incomplete file.
    pub fn allocate(&mut self) -> io::Result<PageId> {
        self.ensure_writable()?;
        let number = self
            .page_count
            .checked_add(1)
            .ok_or_else(|| invalid_input("too many pages"))?;
        let offset = number
            .checked_mul(PAGE_SIZE as u64)
            .ok_or_else(|| invalid_input("page offset overflow"))?;
        let result = self
            .file
            .seek(SeekFrom::Start(offset))
            .and_then(|_| self.file.write_all(&[0; PAGE_SIZE]));
        if let Err(err) = result {
            self.failed = true;
            return Err(err);
        }
        self.page_count = number;
        Ok(PageId(number))
    }
    pub fn read(&mut self, id: PageId) -> io::Result<[u8; PAGE_SIZE]> {
        let offset = self.offset(id)?;
        let mut page = [0u8; PAGE_SIZE];
        self.file.seek(SeekFrom::Start(offset))?;
        self.file.read_exact(&mut page)?;
        Ok(page)
    }
    /// Replace an entire allocated page. A partial I/O failure may damage it.
    pub fn write(&mut self, id: PageId, page: &[u8; PAGE_SIZE]) -> io::Result<()> {
        self.ensure_writable()?;
        let offset = self.offset(id)?;
        let result = self
            .file
            .seek(SeekFrom::Start(offset))
            .and_then(|_| self.file.write_all(page));
        if let Err(err) = result {
            self.failed = true;
            return Err(err);
        }
        Ok(())
    }
    /// Store an allocated root page number, or clear the root. The caller is
    /// responsible for writing referenced pages before publishing a new root.
    pub fn set_root(&mut self, root: Option<PageId>) -> io::Result<()> {
        self.ensure_writable()?;
        if let Some(id) = root {
            self.page_id(id.0)?;
        }
        let number = root.map_or(0, PageId::get);
        let result = self
            .file
            .seek(SeekFrom::Start(ROOT_OFFSET))
            .and_then(|_| self.file.write_all(&number.to_le_bytes()));
        if let Err(err) = result {
            self.failed = true;
            return Err(err);
        }
        self.root = root;
        Ok(())
    }
    /// Flush file data and metadata. This does not make a multi-page update atomic.
    pub fn sync(&mut self) -> io::Result<()> {
        self.ensure_writable()?;
        if let Err(err) = self.file.sync_all() {
            self.failed = true;
            return Err(err);
        }
        Ok(())
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU64, Ordering};
    static NEXT: AtomicU64 = AtomicU64::new(0);
    fn path() -> std::path::PathBuf {
        std::env::temp_dir().join(format!(
            "cs1010-pager-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ))
    }
    #[test]
    fn pages_and_root_survive_reopen() {
        let path = path();
        {
            let mut p = Pager::open(&path).unwrap();
            assert_eq!(p.page_count(), 0);
            assert_eq!(p.root(), None);
            let first = p.allocate().unwrap();
            let second = p.allocate().unwrap();
            assert_eq!(first.get(), 1);
            assert_eq!(second.get(), 2);
            assert_eq!(p.read(second).unwrap(), [0; PAGE_SIZE]);
            let mut data = [0; PAGE_SIZE];
            data[0] = 42;
            data[PAGE_SIZE - 1] = 99;
            p.write(first, &data).unwrap();
            p.set_root(Some(first)).unwrap();
            p.sync().unwrap();
            assert!(p.page_id(0).is_err());
            assert!(p.page_id(3).is_err());
        }
        {
            let mut p = Pager::open(&path).unwrap();
            assert_eq!(p.page_count(), 2);
            let root = p.root().unwrap();
            let data = p.read(root).unwrap();
            assert_eq!((data[0], data[PAGE_SIZE - 1]), (42, 99));
            p.set_root(None).unwrap();
            p.sync().unwrap();
        }
        assert_eq!(Pager::open(&path).unwrap().root(), None);
        std::fs::remove_file(path).unwrap();
    }
    #[test]
    fn rejects_bad_headers_and_partial_pages() {
        let path = path();
        std::fs::write(&path, b"not a page file").unwrap();
        assert!(Pager::open(&path).is_err());
        let mut header = [0u8; PAGE_SIZE];
        header[..8].copy_from_slice(MAGIC);
        header[8..16].copy_from_slice(&1u64.to_le_bytes());
        std::fs::write(&path, header).unwrap();
        assert!(Pager::open(&path).is_err());
        header[8..16].fill(0);
        let mut bytes = header.to_vec();
        bytes.push(7);
        std::fs::write(&path, bytes).unwrap();
        assert!(Pager::open(&path).is_err());
        std::fs::remove_file(path).unwrap();
    }
}
