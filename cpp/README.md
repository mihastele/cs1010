# C++ educational database

The project currently provides in-memory ordered sets of integer keys:

- `RedBlackTree` supports insertion, lookup, and removal.
- `Tree23` provides a 2–3 tree using left-leaning red-black links: a black node and its red left child represent one 3-node.
- `BPlusTree` supports insertion, lookup, removal, and inclusive range scans through linked leaves.
- `BTree` supports insertion, lookup, and removal with a configurable minimum degree of at least 2. `Tree234` specializes it to degree 2, giving two to four children per internal node.

In the B+ tree, all keys reside in linked leaves, and internal separators direct searches. A B-tree splits full nodes by promoting a median key. During deletion, it borrows from siblings or merges nodes to preserve minimum occupancy and equal leaf depth. Nodes can hold many keys, which makes B-trees useful for database indexes where each node can correspond to a storage page. These implementations are in-memory exercises; persistence and database operations remain future work.

Configure with `cmake -S . -B build`, build with `cmake --build build`, and run tests with `ctest --test-dir build --output-on-failure`.
