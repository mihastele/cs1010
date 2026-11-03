# Project guidance

## Purpose

This repository is an educational project for learning how classic data structures support the design and implementation of a database. Favor clarity, correctness, and explanations that help readers understand the algorithms and their tradeoffs.

## Repository organization

Build three independent root-level projects:

- `rust/` for the Rust implementation.
- `c/` for the C implementation.
- `cpp/` for the C++ implementation.

Each project should use conventions and tooling appropriate to its language and remain understandable and buildable on its own. Keep equivalent concepts and behavior aligned across the three implementations while allowing idiomatic language-specific designs.

## Data structures and database

Implement and study these classic data structures in all three projects:

- Red-black tree.
- B-tree.
- B+ tree.
- 2–3 tree.
- 2–3–4 tree.

Use these data structures as foundations for an educational database in each language. Explain how each structure works and how it relates to database concerns such as indexing and storage. The database is intended for learning; do not assume production readiness.

## Development principles

- Explain important invariants, algorithms, and design decisions in clear documentation and comments where they aid learning.
- Test behavior and structural invariants, including edge cases, for each implementation.
- Provide language-specific build, test, and usage instructions as each project is created.
- Keep changes focused and preserve consistency across languages where practical; do not sacrifice idiomatic code for identical implementations.
