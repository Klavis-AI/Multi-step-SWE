set -u
S="$SQLITE3"
# PROFILE.md §5: PRAGMA cache_size as a query must parse and run. The value it
# reports is not pinned (the same paragraph says it is not compared), so this
# case only asserts one integer row.
out=$($S -init /dev/null -batch main.db "PRAGMA cache_size;")
n=$(printf '%s' "$out" | grep -c '^' || true)
if [ "$n" -eq 1 ] && [ "$out" -eq "$out" ] 2>/dev/null; then
    echo "one_integer_row"
else
    echo "not_one_integer_row"
fi
