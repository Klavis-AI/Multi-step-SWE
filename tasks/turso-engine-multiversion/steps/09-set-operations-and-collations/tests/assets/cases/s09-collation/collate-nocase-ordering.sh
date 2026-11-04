# R09.6: expression-level COLLATE NOCASE in ORDER BY and in an equality, against BINARY.
# COLLATE NOCASE orders ASCII letters case-insensitively and makes an equality
# test match regardless of case, while the default BINARY collation stays
# byte-ordered.
#
# Before v0.0.21 the collation is not applied: ordering falls back to BINARY
# and the case-insensitive equality misses. Stock sqlite3 builds the table.
#
# DELIBERATELY NOT group_concat. This case is milestone 9's gate: it vetoes the
# whole step at every later milestone and earns nothing, so every line it
# grades must be an order the normative specs REQUIRE, not merely one this
# engine happens to produce. `lang_aggfunc.html` -- which instruction.md points
# the agent at -- states that group_concat's concatenation order is arbitrary
# unless the ORDER BY is an argument of the aggregate itself; a subquery's
# ORDER BY does not bind it. An engine that concatenated in another order would
# be conforming and would still fail this gate, scoring zero for a correct
# solution. The orderings below are therefore asserted one row per line through
# a top-level ORDER BY, which IS defined, over a total order (b, then the
# unique a). Do not "tidy" these back into a group_concat.
set -u

"$SQLITE3" -batch main.db <<'SQL'
CREATE TABLE u(a INTEGER, b TEXT);
INSERT INTO u VALUES(1,'B'),(2,'a'),(3,'b'),(4,'A'),(5,'c');
SQL

"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*│/d; /^[[:space:]]*$/d; s/^.*×.*$/ERROR/'
SELECT 'eq-nocase:'||count(*) FROM u WHERE b='a' COLLATE NOCASE;
SELECT 'eq-binary:'||count(*) FROM u WHERE b='a';
SELECT 'sort-nocase:'||b FROM u ORDER BY b COLLATE NOCASE, a;
SELECT 'sort-binary:'||b FROM u ORDER BY b, a;
SQL

# Differential: a .sh case is not covered by the generator's automatic sqlite3
# replay, so the case makes the comparison itself. Row-per-line, same reason.
"$SQLITE3" -batch -list -noheader main.db "SELECT 'sqlite-sort-nocase:'||b FROM u ORDER BY b COLLATE NOCASE, a;"
echo "sqlite-eq:$("$SQLITE3" -batch -list -noheader main.db "SELECT count(*) FROM u WHERE b='a' COLLATE NOCASE;")"
