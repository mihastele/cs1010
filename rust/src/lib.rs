//! Educational in-memory tree implementations.

/// Left-leaning red-black tree ordered set.
pub mod red_black_tree;

// Preserve the original crate-root path for existing users.
pub use red_black_tree::RedBlackTree;

/// B-tree ordered set.
pub mod btree;

/// 2â€“3â€“4 tree ordered set.
pub mod tree234;

/// 2â€“3 tree encoded with left-leaning red-black links.
pub mod tree23;

/// B+ tree ordered set.
pub mod bplus;

/// Persistent table storage backed by a B+ tree index.
pub mod storage;

/// Minimal SQL database over persistent B+ tree indexed tables.
pub mod database;
