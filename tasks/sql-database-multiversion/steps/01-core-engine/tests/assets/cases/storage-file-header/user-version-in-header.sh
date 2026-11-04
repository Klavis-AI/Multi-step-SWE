set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a); PRAGMA user_version = 258;" || exit 1
# offset 60, 4 bytes, big-endian signed: the user version
od -An -tu1 -j60 -N4 -- main.db | tr -s ' ' | sed -e 's/^ *//' -e 's/ *$//'
$S -init /dev/null -batch main.db "PRAGMA user_version;"
