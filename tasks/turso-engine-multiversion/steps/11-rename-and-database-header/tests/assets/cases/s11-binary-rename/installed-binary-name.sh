# R11.1 and R11.2: the shell answers SQL from the path it is installed at, and
# nothing is left behind under the retired name beside it. Two things are
# observed: whether anything named `limbo` still sits in the same directory,
# and whether the file at the new name is the engine itself rather than a stub
# placed there.
#
# A third line used to echo `basename "$CLI"`. It could never fail: test.sh
# exports CLI="$GRADED_CLI" for every scored group, so that basename is the
# contract's own name for every candidate, one that performed the rename and
# one that did not alike. A line whose value no candidate can change grades
# nothing, so it is gone. R11.1 is carried by the SQL answer below -- which
# comes from the binary at the graded path and from nowhere else -- and by
# test.sh's prerequisite that the path is a regular, non-symlink, executable
# file.
#
# The retired-name probe is `-e` or `-L`, not `-x`. TEST_INTERFACE.md says the
# old path must no longer exist "not as a file, a copy, a hard link or a
# symlink", and `-x` alone is looser than that sentence: a non-executable
# regular file left at the name passes it, and so does a dangling symlink,
# which `-e` follows and therefore also misses -- hence the explicit `-L`.
#
# No second opinion is possible here and none is wanted: the absence of a
# sibling file is a property of the filesystem, not of any SQL, so stock
# sqlite3 has nothing to say about it. The one SQL answer below is a constant
# fold, checked against arithmetic.
set -u
if [ -e "$(dirname "$CLI")/limbo" ] || [ -L "$(dirname "$CLI")/limbo" ]; then
    echo "retired_name_present yes"
else
    echo "retired_name_present no"
fi
# The SQL arrives on standard input, which is the invocation TEST_INTERFACE.md
# documents: the flags it lists plus one positional, the database path. An
# earlier version of this case passed the SQL as a *second* positional -- a
# grammar no task document states, and one a shell that implemented only the
# documented surface would reject with exit 2 under PROFILE rule 8.
#
# Standard error is discarded rather than merged, because this is graded
# engine output and a merged warning line would land inside it.
"$CLI" -m "$CLI_MODE" -q main.db 2>/dev/null <<'SQL' | sed '/^[[:space:]]*$/d'
SELECT 6*7;
SQL
