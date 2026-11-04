# R07.3: an index never changes a query's answer -- the same query over an
# indexed table and over an unindexed copy of the same rows returns identical
# rows, sums and counts.
#
# Tables t and u hold identical rows; only t is indexed, with a two-column
# index whose leading key is an INTEGER. Stock sqlite3 builds both, so the
# engine's only job is to answer the same question twice and get the same
# answer. Each line carries its own tag, so a disagreement between the indexed
# and the unindexed plan shows up as a differing line rather than a diagnostic.
set -u

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE t(k TEXT, v INTEGER);
CREATE TABLE u(k TEXT, v INTEGER);
INSERT INTO t VALUES('alpha',1),('bravo',2),('charlie',3),('delta',4),('echo',5);
INSERT INTO u SELECT k,v FROM t;
CREATE INDEX i_t_vk ON t(v,k);
SQL

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*$/d'
SELECT 'idx-ge:'||k||'='||v FROM t WHERE v>=3 ORDER BY v,k;
SELECT 'tab-ge:'||k||'='||v FROM u WHERE v>=3 ORDER BY v,k;
SELECT 'idx-lt:'||k||'='||v FROM t WHERE v<3 ORDER BY v,k;
SELECT 'tab-lt:'||k||'='||v FROM u WHERE v<3 ORDER BY v,k;
SELECT 'idx-sum:'||sum(v) FROM t WHERE v>2;
SELECT 'tab-sum:'||sum(v) FROM u WHERE v>2;
SELECT 'idx-eq:'||count(*) FROM t WHERE v=4;
SELECT 'tab-eq:'||count(*) FROM u WHERE v=4;
SQL
