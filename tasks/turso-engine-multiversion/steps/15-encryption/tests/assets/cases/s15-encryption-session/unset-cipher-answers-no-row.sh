# R15.11: the cipher pragma's query form answers no row at all when nothing is
# selected -- not an empty string and not a NULL row.
#
# The distinction is invisible to the .sql path, which strips blank lines, and
# invisible to any fold that drops them: a single NULL or zero-length text
# column renders as an empty line, so an engine answering an empty row would
# look exactly like one answering nothing. This case therefore reads raw
# stdout, and compares it to stock sqlite3's rendering of the same script.
# Stock ignores a pragma name it does not recognise -- measured against the
# pinned 3.50.2 fixture, which runs it with no diagnostic, no row and exit 0 --
# so its stdout is the two tagged rows and nothing else, which is the framing
# the shell's own stdout has matched since step 11's checkpoint, where `R11.3`
# states it: result rows only, with no blank separator lines between them.
# An engine that answered an empty row, or that framed its blocks with blank
# lines, differs from stock by a line and fails here.
#
# The second half is the positive arm, on the same two tags: with a cipher
# selected the query form answers exactly one line -- the canonical name --
# between them, so the emptiness of the first half is not the emptiness of a
# shell that never answers this pragma at all.
set -u
cat > unset.sql <<'SQL'
SELECT 'tag-before';
PRAGMA cipher;
SELECT 'tag-after';
SQL
"$CLI" -m "$CLI_MODE" -q main.db < unset.sql > engine.txt 2>/dev/null
"$SQLITE3" -batch -list -separator '|' -noheader stock.db < unset.sql > stock.txt 2>/dev/null
echo "unset query form:"
cat engine.txt
if cmp -s engine.txt stock.txt
then echo "matches stock sqlite3 rendering: yes"
else echo "matches stock sqlite3 rendering: no"
fi
cat > selected.sql <<'SQL'
PRAGMA cipher = 'aegis256';
SELECT 'tag-before';
PRAGMA cipher;
SELECT 'tag-after';
SQL
echo "with a cipher selected:"
"$CLI" -m "$CLI_MODE" -q main.db < selected.sql 2>/dev/null
