# R11.3: the shell's stdout for a multi-statement script is graded byte for
# byte here, with nothing filtered. Earlier checkpoints emitted a blank
# separator line ahead of every result block, so a script's output could not
# be concatenated or diffed against another reader's without stripping them
# first. From this checkpoint the result lines are the whole of stdout.
#
# R11.3's promise is that the output is "comparable byte-for-byte to another
# reader's rendering of the same rows", so the second half of this case makes
# that comparison rather than leaving it as a claim: the identical script is
# run through stock sqlite3, in its own directory, in the same rendering mode
# (`-list`, `|` separator, no header), and the two outputs are compared. The
# `2>&1` on the stock invocation is deliberate: a stock failure then shows up
# as a mismatch instead of as an empty file that happens to compare equal to
# nothing.
set -u
cat > script.sql <<'SQL'
CREATE TABLE t(a INTEGER, b TEXT);
INSERT INTO t VALUES(1,'one'),(2,'two');
SELECT a FROM t ORDER BY a;
SELECT b FROM t ORDER BY a;
SELECT a, b FROM t ORDER BY a;
SELECT count(*) FROM t;
SQL
mkdir engine stock
( cd engine && "$CLI" -m "$CLI_MODE" -q main.db < ../script.sql ) > engine.out 2>/dev/null
( cd stock && "$SQLITE3" -batch -list -separator '|' -noheader main.db < ../script.sql ) > stock.out 2>&1
cat engine.out
if cmp -s engine.out stock.out; then echo "same_as_stock yes"; else echo "same_as_stock no"; fi
