# R15.13's second half: re-selecting the cipher before a key is held is a
# *change*, and the newest selection is the one a later key is checked against.
#
# `cipher-selection.sql` selects the same cipher twice in two spellings, so it
# grades canonicalisation and tolerance but not a change of selection. Here two
# different ciphers are selected in turn: the query form reports the second,
# a key of the size the *first* would have wanted is refused, and a key of the
# size the second wants is taken and encrypts -- the reserved-space byte at
# page-1 offset 20 is the value `spec/PROFILE.md` fixes for `aegis256`, `30` in
# hex, which a session still holding `aegis128l` could not produce (it fixes
# `28`). So the graded claim is that the newest selection is the binding one.
#
# The wording of a diagnostic is not graded: every line carrying the shell's
# box-drawing marker is folded to the single word ERROR.
set -u
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
PRAGMA cipher = 'aegis128l';
PRAGMA cipher = 'aegis256';
PRAGMA cipher;
PRAGMA hexkey = '000102030405060708090a0b0c0d0e0f';
PRAGMA hexkey = '000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f';
CREATE TABLE t(id INTEGER PRIMARY KEY, v TEXT);
INSERT INTO t VALUES(1,'encrypted under the newest selection');
SELECT id, v FROM t;
SQL
echo "reserved_space $(od -An -tx1 -j 20 -N 1 main.db | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')"
