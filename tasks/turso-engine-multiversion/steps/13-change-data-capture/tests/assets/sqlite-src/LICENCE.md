# SQLite — public domain

The files `sqlite3.c`, `sqlite3.h` and `shell.c` in this directory are the
SQLite 3.50.2 amalgamation, taken verbatim from the official distribution at
https://sqlite.org/2025/sqlite-amalgamation-3500200.zip and pinned by the
SHA-256 manifest in `SHA256SUMS`.

SQLite is in the public domain. The upstream notice reads:

> The author disclaims copyright to this source code. In place of a legal
> notice, here is a blessing:
>
>   * May you do good and not evil.
>   * May you find forgiveness for yourself and forgive others.
>   * May you share freely, never taking more than you give.

They are compiled inside the verifier image and used only as a differential
oracle, a format validator and an interoperability peer. They are never
linked to, copied into, or made visible to the candidate's workspace.
