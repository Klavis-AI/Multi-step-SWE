# R01.16: an unusable command-line argument is a process-level failure.
#
# R01.16 says that an argument the shell cannot use makes that invocation exit
# `2`, write nothing at all to standard output, and leave no database file
# behind, while the same statement stream with a usable argument still runs and
# still answers. All four of those are on the single labelled line below, and
# the same script is then run again with a usable argument to show that the
# refusal is about the argument and not about the SQL. `spec/PROFILE.md` rule 8
# is the normative statement of the same contract.
#
# No stock sqlite3 cross-read, and one would assert something false. Exit `2`
# is this profile's choice, not the format's or SQLite's: stock sqlite3 given
# an unrecognised flag prints a usage message and exits `1`, and it is still
# free to create the database file. Comparing the two would record a
# disagreement that `spec/PROFILE.md` rule 8 deliberately creates. Nothing here
# is a SQL result except the surviving `count(*)`, which the `.sql` cases in
# `s01-dml-and-select` already cross-read.
# An argument the engine cannot use is a process-level failure, not a
# diagnostic it reports and recovers from: the interface fixes exit 2 for it
# and promises a panic is the only other non-zero exit. A refusal that instead
# printed a message and returned 0 would be indistinguishable from success to
# anything that only reads the shell's output, and would let a mistyped flag
# leave a database behind as if the command had run.
#
# The wording is not graded. The failing invocation's diagnostic goes to
# stderr, which is never compared, so what is asserted is the contract and
# nothing about the message: the failing command writes nothing at all to
# stdout and creates no file, while the same statement stream with a usable
# argument still runs and still answers. The count of stdout bytes is printed
# as a number rather than embedded, so a renderer that echoes the message onto
# stdout is caught here instead of slipping through as the length of a word.
#
# Separator blank lines around the surviving result block come and go along the
# release chain and are removed, exactly as the .sql grading path removes them.
# The one row selected is a bare count on a line of its own and the lines
# around it are this case's own labelled output, so nothing removed could have
# been a result row.
set -u
printf 'CREATE TABLE t(a);\nINSERT INTO t VALUES(1),(2);\nSELECT count(*) FROM t;\n' > w.sql
bad="$("$CLI" -m "$CLI_MODE" -q --no-such-flag main.db < w.sql 2>/dev/null)"; bad_rc=$?
echo "unusable-argument-exit=$bad_rc stdout-bytes=${#bad} database-written=$([ -e main.db ] && echo yes || echo no)"
"$CLI" -m "$CLI_MODE" -q main.db < w.sql 2>/dev/null > good.out; good_rc=$?
sed '/^[[:space:]]*$/d' good.out
echo "usable-argument-exit=$good_rc"
