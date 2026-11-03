# Rust educational database

The library contains in-memory ordered sets of `i32` keys:

- `RedBlackTree`: insert, lookup, and remove using left-leaning red-black balancing.
- `tree23::Tree23` provides a 2–3 tree using left-leaning red-black links: a black node and its red left child represent one 3-node.
- `bplus::BPlusTree`: insert, lookup, remove, and inclusive range scans over linked leaves.
- `btree::BTree`: insert, lookup, and remove with a configurable minimum degree of at least 2.
- `tree234::Tree234`: a B-tree of minimum degree 2, whose internal nodes have two to four children.

The B-tree promotes a median when a node splits. Before deletion descends into a minimally occupied child, it borrows from a sibling or merges nodes. These operations preserve occupancy and equal leaf depth. In the B+ tree, internal separators direct searches while linked leaves hold all keys and make range scans efficient. B-trees are useful for database indexes because a node can hold many keys, reducing the number of storage pages accessed during lookup. These implementations are in-memory exercises, not persistent storage engines.

Run `cargo test` from this directory. Storage and database operations will be added later.
