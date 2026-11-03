# C++ educational database

The project currently provides in-memory ordered sets of integer keys:

- `RedBlackTree` supports insertion, lookup, and removal.
- `Tree23` provides a 2–3 tree using left-leaning red-black links: a black node and its red left child represent one 3-node.
- `BPlusTree` supports insertion, lookup, removal, and inclusive range scans through linked leaves.
- `BTree` supports insertion, lookup, and removal with a configurable minimum degree of at least 2. `Tree234` specializes it to degree 2, giving two to four children per internal node.
- `Table` persists integer keys with byte values in a single file, indexed by a B+ tree; see below.

In the B+ tree, all keys reside in linked leaves, and internal separators direct searches. A B-tree splits full nodes by promoting a median key. During deletion, it borrows from siblings or merges nodes to preserve minimum occupancy and equal leaf depth. Nodes can hold many keys, which makes B-trees useful for database indexes where each node can correspond to a storage page.

## Persistent storage foundation

`Table` stores integer primary keys and byte values in a single append-only file: an 8-byte `CSDB` header followed by insert and delete records with little-endian integers, a format shared with the C and Rust implementations. It rebuilds an in-memory B+ tree index on open, and supports insert, lookup, delete, and ordered inclusive range scans. Values are cached in memory. Each successful mutation is written and synced before it returns. Failures throw exceptions; after a write failure the table must be destroyed and reopened, which replays the log.

This is an initial storage layer, not yet a SQL database. It does not support multiple tables, SQL, transactions, concurrent writers, corruption checksums, or SQLite files. Truncated records cause opening to fail; there is no automatic recovery. Use a single writer and keep backups of data.

Configure with `cmake -S . -B build`, build with `cmake --build build`, and run tests with `ctest --test-dir build --output-on-failure`.
## Minimal SQL interface

`Database` in `database.hpp` opens a directory of persistent tables. `execute` accepts one statement at a time:

```sql
CREATE TABLE people (id INTEGER PRIMARY KEY, value TEXT);
INSERT INTO people VALUES (1, 'Ada');
SELECT * FROM people;
SELECT * FROM people WHERE id = 1;
DELETE FROM people WHERE id = 1;
```

Each table uses this fixed schema. Keywords and identifiers are case-insensitive; double an apostrophe inside a string. Results are returned as `Database::Result`, with selected rows ordered by ID. This is an educational SQL subset with no transactions, locking, or SQLite file compatibility. Only one writer may access a database at a time. Like the underlying table store, it rejects truncated records and does not repair them automatically.
