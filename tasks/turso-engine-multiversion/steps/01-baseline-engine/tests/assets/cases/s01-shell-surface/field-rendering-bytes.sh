# R01.12: the exact bytes of a rendered result row.
#
# R01.12 fixes the rendering when the output mode is named on the command line:
# one line per row, a single `|` between columns, no header, no padding, and
# NULL and blob values in the profile's renderings. This case selects a value
# of every storage class -- NULL, empty text, a zero-length blob, text with
# spaces, a negative integer, a real, text containing a literal `|`, and a
# non-empty blob -- and pipes the output through `sed -n l`, which makes every
# byte of every line visible. A renderer that pads, quotes, or spells NULL out
# as a word is caught even though the values themselves are unremarkable.
#
# No stock sqlite3 cross-read, by the requirement's own wording: R01.12 grades
# the profile's renderings. `spec/PROFILE.md` rules 4 and 5 fix them -- NULL as
# the empty string with no `-nullvalue` override, and a non-empty blob as its
# raw bytes -- and for these two classes those rules are also what stock
# sqlite3's own `-list` mode prints, which is why a cross-read would
# discriminate nothing here. The `typeof()` and `length()` columns, which are
# ordinary SQL, are cross-read by `s01-file-interop/serial-type-roundtrip.sql`.
# The exact bytes of a result row: the column separator is a single '|', there
# is no padding, no quoting and no trailing whitespace, and NULL, the empty
# string and a zero-length blob each render as an empty field rather than as a
# word like "NULL". Passing the output through `sed -n l` makes every byte of
# every line visible, so a renderer that pads columns or spells NULL out is
# caught here even though the values themselves are unremarkable.
#
# Separator blank lines between result blocks come and go along the release
# chain and are removed here, exactly as the .sql grading path removes them.
# No single-column NULL row is selected, so nothing that is removed could have
# been a result row.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > raw.txt <<'SQL'
CREATE TABLE r(k INTEGER PRIMARY KEY, v);
INSERT INTO r VALUES(1,NULL);
INSERT INTO r VALUES(2,'');
INSERT INTO r VALUES(3,x'');
INSERT INTO r VALUES(4,'text with spaces');
INSERT INTO r VALUES(5,-1);
INSERT INTO r VALUES(6,3.5);
INSERT INTO r VALUES(7,'pipe|inside');
INSERT INTO r VALUES(8,x'414243');
SELECT k, v FROM r ORDER BY k;
SELECT k, typeof(v), coalesce(length(v),-1) FROM r ORDER BY k;
SELECT count(*) FROM r;
SQL
sed -e '/^[[:space:]]*$/d' raw.txt | sed -n l
