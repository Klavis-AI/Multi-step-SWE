# R10.10: page-1 header bytes 48..51 of a freshly created database hold the
# default page-cache size as big-endian signed -2000 (ff ff f8 30), PRAGMA
# cache_size reports -2000, and stock sqlite3's integrity_check answers ok.
#
# Bytes 48..51 of the page-1 header hold the suggested default page-cache
# size. SQLite's own default is -2000, a negative value meaning "2000 KiB"
# rather than a page count. The two previous checkpoints (v0.0.20, v0.0.21)
# wrote 500 there -- a positive value that tells every later reader to size
# its cache by pages instead -- and PRAGMA cache_size reported 500 to match.
# This checkpoint corrects the field to -2000, and PRAGMA cache_size must
# report the same value the header carries. Both readings are graded: the raw
# bytes from the file, and the pragma's answer from the engine.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE hdr(a);
INSERT INTO hdr VALUES(1);
-- The writing invocation ends by reading back every table it wrote. A
-- conforming engine does not need that: spec/PROFILE.md's durability rule
-- requires a process that writes and then exits to leave its committed data
-- recoverable by the next reader that opens the files. The read-back is this
-- case's own belt and braces, its output is discarded, and nothing below
-- depends on what it printed.
SELECT count(*) FROM hdr;
SQL
echo "default_cache_size $(od -An -tx1 -j 48 -N 4 main.db | tr -s ' \n' ' ' | sed 's/^ //;s/ $//')"
# The pragma is asked on standard input, which is the only input channel the
# contract documents: TEST_INTERFACE.md's command-line table gives the shell
# exactly one positional, the database path, and spec/PROFILE.md rule 8 tells a
# conforming shell to reject an argument it does not recognise -- exit 2, no
# standard output, no database left behind. An earlier version of this line
# passed the SQL as a second positional, which is a surface no agent-visible
# document authorises, so a document-faithful shell failed this case.
cache_size="$("$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*$/d'
PRAGMA cache_size;
SQL
)"
echo "pragma_cache_size $cache_size"
# Stock sqlite3's stderr is merged on purpose: a stock-side failure must land
# in the graded output rather than vanish.
echo "integrity $("$SQLITE3" -batch main.db 'PRAGMA integrity_check;' 2>&1)"
