set -u
S="$SQLITE3"
$S -init /dev/null -batch main.db "CREATE TABLE t(a INTEGER PRIMARY KEY);" || exit 1
$S -init /dev/null -batch main.db "PRAGMA journal_mode;"
for m in delete truncate persist memory off; do
    echo "set=$($S -init /dev/null -batch main.db "PRAGMA journal_mode = $m;")"
done
$S -init /dev/null -batch main.db "PRAGMA journal_mode = delete;" > /dev/null
$S -init /dev/null -batch main.db "PRAGMA journal_mode;"
