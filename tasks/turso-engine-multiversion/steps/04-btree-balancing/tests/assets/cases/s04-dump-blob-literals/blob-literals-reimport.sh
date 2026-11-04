# R04.8: the engine's own `.dump` renders a blob as an X'...' literal, and a
# fresh database that stock sqlite3 builds from that dump holds the identical
# bytes.
#
# Two things are observed and nothing else: the blob literals the dump wrote,
# in row order, and stock sqlite3's own rendering of every value it re-imported
# from that dump. The dump's schema line -- the engine's normalised CREATE
# TABLE text -- is deliberately not compared, because its spacing is a separate
# obligation graded at the step that fixes it, and a case that compared the
# whole dump would fail on that instead of on the blob literals.
#
# The engine's own hex() is not used as the second opinion. At this checkpoint
# it renders a byte above 0x7f as that byte's UTF-8 replacement sequence, so
# hex(x'00ff10') answers 00EFBFBD10 rather than 00FF10; that defect is real and
# is corrected later in the chain, and a case that read the bytes back through
# it would be asserting the defect. Stock sqlite3 is the second opinion, and it
# reads the dump *text*, so nothing here depends on a second process opening
# the engine's database file.
#
# Every value below is one the format renders exactly: a byte above 0x7f, an
# empty blob, a blob whose bytes are all printable-looking hex, and a run that
# mixes the three. A NULL column is deliberately absent -- the reference
# renders one as X'' rather than as NULL from this checkpoint until v0.1.5, and
# freezing that would teach the candidate a bug.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > dump.sql <<'SQL'
CREATE TABLE b(id INTEGER PRIMARY KEY, v BLOB);
INSERT INTO b VALUES(1,x'00ff10');
INSERT INTO b VALUES(2,x'');
INSERT INTO b VALUES(3,x'deadbeef');
INSERT INTO b VALUES(4,x'7f80ffcafe0011');
.dump
SQL
echo "literals:"
grep -oE "X'[0-9a-fA-F]*'" dump.sql
echo "reimported:"
"$SQLITE3" -batch copy.db < dump.sql > /dev/null 2>&1
"$SQLITE3" -batch -list -separator '|' -noheader copy.db \
    'SELECT id, quote(v), length(v) FROM b ORDER BY id;' 2>&1
