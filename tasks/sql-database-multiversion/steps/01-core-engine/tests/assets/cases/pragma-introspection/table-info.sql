CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT NOT NULL, c REAL, d);
PRAGMA table_info(t);
CREATE TABLE u(x, y, PRIMARY KEY(y, x));
PRAGMA table_info(u);
