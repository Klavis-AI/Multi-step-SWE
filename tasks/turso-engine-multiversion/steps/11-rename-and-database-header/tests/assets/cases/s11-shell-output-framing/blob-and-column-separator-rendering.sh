# R11.10 and R11.3: byte-exact stdout again -- the column separator, the
# rendering of a non-empty blob as its raw bytes, and the fact that nothing is
# emitted around a result block. A blob whose bytes are printable text is
# indistinguishable in the row from that text, which is a property of the
# rendering and is pinned here deliberately; typeof(), length() and hex() are
# what tell the two values apart.
#
# spec/PROFILE.md rule 5 says in terms that this is stock sqlite3's `-list`
# rendering, which the profile follows, so stock rendering the same script is
# a real second opinion and not an expected divergence -- that is what the
# comparison below checks. Its standard error is folded into its output on
# purpose, so a stock failure fails the comparison rather than passing
# silently.
set -u
cat > script.sql <<'SQL'
CREATE TABLE t(id INTEGER PRIMARY KEY, b BLOB);
INSERT INTO t VALUES(1,x'414243'),(2,x''),(3,'ABC');
SELECT id, b FROM t ORDER BY id;
SELECT id, typeof(b), length(b), hex(b) FROM t ORDER BY id;
SELECT count(*) FROM t;
SQL
mkdir engine stock
( cd engine && "$CLI" -m "$CLI_MODE" -q main.db < ../script.sql ) > engine.out 2>/dev/null
( cd stock && "$SQLITE3" -batch -list -separator '|' -noheader main.db < ../script.sql ) > stock.out 2>&1
cat engine.out
if cmp -s engine.out stock.out; then echo "same_as_stock yes"; else echo "same_as_stock no"; fi
