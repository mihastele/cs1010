# Rust educational database

The library contains in-memory ordered sets of `i32` keys:

- `RedBlackTree`: insert, lookup, and remove using left-leaning red-black balancing.
- `tree23::Tree23` provides a 2–3 tree using left-leaning red-black links: a black node and its red left child represent one 3-node.
- `bplus::BPlusTree`: insert, lookup, remove, and inclusive range scans over linked leaves.
- `btree::BTree`: insert, lookup, and remove with a configurable minimum degree of at least 2.
- `tree234::Tree234`: a B-tree of minimum degree 2, whose internal nodes have two to four children.

The B-tree promotes a median when a node splits. Before deletion descends into a minimally occupied child, it borrows from a sibling or merges nodes. These operations preserve occupancy and equal leaf depth. In the B+ tree, internal separators direct searches while linked leaves hold all keys and make range scans efficient. B-trees are useful for database indexes because a node can hold many keys, reducing the number of storage pages accessed during lookup. These implementations are in-memory exercises, not persistent storage engines.

Run `cargo test` from this directory.

## Persistent storage foundation

`storage::Table` stores integer primary keys and byte values in a single append-only file. It rebuilds an in-memory B+ tree index on open, and supports insert, lookup, delete, and ordered inclusive range scans. Each successful mutation syncs the log before returning.

This is an educational storage layer. It does not support transactions, concurrent writers, corruption checksums, or SQLite files. Truncated records cause opening to fail; there is no automatic recovery. A failed write can leave a partial record, and the table must be reopened (and potentially repaired manually). Use a single writer and keep backups of data.

## Minimal SQL interface

`database::Database::open(directory)` opens a directory of tables. Call `execute` with one of these statements (identifiers and keywords are case-insensitive; strings use SQL doubled apostrophes):

```sql
CREATE TABLE people (id INTEGER PRIMARY KEY, value TEXT);
INSERT INTO people VALUES (1, 'Ada');
SELECT * FROM people;
SELECT * FROM people WHERE id = 1;
DELETE FROM people WHERE id = 1;
```

Each table has exactly this two-column schema; IDs are signed 32-bit integers and values are UTF-8 text. Results are returned as `QueryResult`; selection returns rows in primary-key order. This is a small SQL subset, not SQLite compatibility. Only one writer may access a database at a time; no locking is implemented. The C++ project also provides a SQL subset; the C project has table storage but no SQL layer yet.

## Page manager foundation

`pager::Pager` manages a separate file of fixed 4096-byte pages. Page zero contains a magic/version header and an optional root page ID; subsequent pages can be allocated, read, and written by stable page number. `sync` requests that file contents and metadata be flushed.

The pager is not connected to the SQL table store or B+ tree yet: tables still use the append-only log and rebuild their indexes in memory. The pager does not implement free-page reuse, locking, checksums, transactions, or crash-atomic multi-page writes. A failed or interrupted write can corrupt the file; this is a learning foundation, not crash-safe storage.

## Page-backed B+ tree index

`disk_bplus::DiskBPlusTree` stores integer keys in B+ tree pages managed by `pager::Pager`. It currently supports insertion, lookup, inclusive range scans, and explicit `sync`; reopening reads the persisted tree rather than rebuilding it from a log. It is a standalone index and is not yet connected to the SQL table store. Deletion, page reuse, transactions, recovery, and multi-page crash atomicity remain unimplemented.
