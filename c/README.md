# C educational database

The project currently provides in-memory ordered sets of integer keys:

- `rb_tree` supports insertion, lookup, and removal.
- `tree23` provides a 2–3 tree using left-leaning red-black links: a black node and its red left child represent one 3-node.
- `bplus` supports insertion, lookup, removal, and inclusive range scans through linked leaves.
- `btree` supports insertion, lookup, and removal with a configurable minimum degree of at least 2. Degree 2 is a 2–3–4 tree: each internal node has two to four children.
- `table` persists integer keys with byte values in a single file, indexed by a B+ tree; see below.

Call `btree_init` before use and `btree_destroy` afterward. `btree_insert` reports allocation failure in its return value and reports whether the key was new through its `inserted` argument. It reserves nodes before modifying the tree, so allocation failure leaves the tree unchanged.

`bplus_range` returns the total number of matches and writes at most the supplied capacity; pass a null output with capacity zero to count matches. The B+ tree also reserves nodes before insertion so allocation failure leaves it unchanged. Internal separators direct searches to linked leaves containing all keys.

B-trees split full nodes by promoting a median key; deletion borrows or merges nodes to maintain occupancy and equal leaf depth. A node can hold many keys, making B-trees useful for database indexes where a node can correspond to a storage page.

## Persistent storage foundation

`table` stores integer primary keys and byte values in a single append-only file: an 8-byte `CSDB` header followed by insert and delete records with little-endian integers, a format shared with the Rust and C++ implementations. It rebuilds an in-memory B+ tree index on open, and supports insert, lookup, delete, and ordered inclusive range scans. Values are cached in memory, sorted by key for binary search. Each successful mutation is written and synced before it returns.

This is an initial storage layer, not yet a SQL database. It does not support multiple tables, SQL, transactions, concurrent writers, corruption checksums, or SQLite files. Truncated records cause opening to fail; there is no automatic recovery. After a write failure the table refuses further mutations until it is closed and reopened, which replays the log. Use a single writer and keep backups of data.

Configure with `cmake -S . -B build`, build with `cmake --build build`, and run tests with `ctest --test-dir build --output-on-failure`.
