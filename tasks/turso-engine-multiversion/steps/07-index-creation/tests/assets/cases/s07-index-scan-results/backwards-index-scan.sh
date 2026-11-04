# R07.5: a backwards scan of an index on an INTEGER key and on a TEXT key
# returns rows in descending key order, under LIMIT as well, and min()/max()
# over a bounded range agree with that order.
#
# `ORDER BY <indexed column> DESC` combined with a lower-bound predicate can be
# answered by walking the index from its high end. Before v0.0.19 the engine
# walks it forwards and hands back ascending rows, so the row ORDER is wrong
# even though the row SET is right; the ordering is what this case pins down.
# Stock sqlite3 builds the indexes, so nothing here depends on the engine being
# able to create one -- which it cannot be asked to do at this step; see
# "What this milestone does not include" in TEST_INTERFACE.md.
set -u

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT, c INTEGER);
INSERT INTO t VALUES(1,'p',10),(2,'q',20),(3,'r',30),(4,'s',40),(5,'t',50),(6,'u',60);
CREATE INDEX i_c ON t(c);
CREATE INDEX i_b ON t(b);
SQL

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
SELECT 'int-desc:'||c FROM t WHERE c>25 ORDER BY c DESC;
SELECT 'int-desc-limit:'||c FROM t WHERE c>=20 ORDER BY c DESC LIMIT 3;
SELECT 'text-desc:'||c FROM t WHERE b>'q' ORDER BY b DESC;
SELECT 'full-desc:'||c FROM t ORDER BY c DESC;
SELECT 'asc-unchanged:'||c FROM t WHERE c>15 AND c<55 ORDER BY c;
SELECT 'min-above:'||min(c) FROM t WHERE c>25;
SELECT 'max-below:'||max(c) FROM t WHERE c<45;
SQL
