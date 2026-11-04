# R07.11: a range predicate resolved through an index whose leading key is TEXT
# returns exactly the rows the range describes, for >, >=, < and =, and the
# counts over those ranges agree.
#
# The index is built by stock sqlite3, not by this engine, so the case asserts
# only that the engine *reads* a b-tree index correctly; it does not depend on
# CREATE INDEX being available in the engine at any particular checkpoint.
# Before v0.0.19 the engine ignores the index and the range bounds resolve
# against the wrong key, so the row set it returns is not the one the range
# describes.
#
# The engine's output is piped through sed because checkpoints v0.0.20-v0.0.22
# print a blank line ahead of every result block; stripping it here keeps the
# graded bytes identical across the whole chain.
set -u

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT, c INTEGER);
INSERT INTO t VALUES(1,'a',10),(2,'b',20),(3,'c',30),(4,'d',40),(5,'e',50);
CREATE INDEX i_bc ON t(b,c);
SQL

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*$/d'
SELECT 'gt:'||b||':'||c FROM t WHERE b>'b' ORDER BY b,c;
SELECT 'ge:'||b||':'||c FROM t WHERE b>='c' ORDER BY b,c;
SELECT 'lt:'||b||':'||c FROM t WHERE b<'c' ORDER BY b,c;
SELECT 'eq:'||b||':'||c FROM t WHERE b='d' ORDER BY b,c;
SELECT 'count-gt:'||count(*) FROM t WHERE b>'b';
SELECT 'count-lt:'||count(*) FROM t WHERE b<'c';
SQL
