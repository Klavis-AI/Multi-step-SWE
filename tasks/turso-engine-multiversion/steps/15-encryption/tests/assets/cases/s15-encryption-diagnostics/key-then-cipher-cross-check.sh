# R15.12: the pair is cross-checked at the moment the second of the two
# arrives, in either order.
#
# Every other case at this step supplies the cipher first. Here the key arrives
# first -- it is taken, because a key on its own has nothing to be checked
# against -- and the cipher is the one that completes the pair, so the
# mismatch is detected at the cipher rather than at the key. What is asserted
# afterwards is the state both readings of that refusal agree on: the key is
# still held, the session carries on, and no encryption context is ever built
# from the mismatched pair, so the file stays an ordinary unencrypted file.
# That last part is what the reserved-space byte and stock sqlite3's own read
# of the file show, and it is the reason this case writes and reads a table
# rather than stopping at the refusal.
#
# The reverse arrival order is stated in TEST_INTERFACE.md ("the pair is
# cross-checked at the moment the second of the two arrives") without
# designating which arrival that is; this case is what makes the sentence
# cover the order the other cases do not use.
#
# The wording of a diagnostic is not graded: every line carrying the shell's
# box-drawing marker is folded to the single word ERROR, so what is asserted
# is that a refusal landed at exactly that point in the stream and that the
# statements after it still ran.
set -u
K=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
"$CLI" -m "$CLI_MODE" -q main.db <<SQL | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
PRAGMA hexkey = '$K';
PRAGMA cipher = 'aegis128l';
PRAGMA hexkey;
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'plaintext');
SELECT id, v FROM t;
SQL
echo "reserved_space $(od -An -tx1 -j 20 -N 1 main.db | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')"
echo "rows:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db 'SELECT id,v FROM t ORDER BY id;' 2>&1
