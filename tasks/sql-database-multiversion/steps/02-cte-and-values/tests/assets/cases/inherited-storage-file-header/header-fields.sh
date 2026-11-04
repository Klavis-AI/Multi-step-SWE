set -u
S="$SQLITE3"
byte() { od -An -tu1 -j "$1" -N "$2" -- main.db | tr -s ' ' | sed -e 's/^ *//' -e 's/ *$//'; }

$S -init /dev/null -batch main.db \
  "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT); INSERT INTO t VALUES(1,'hello');" || exit 1

# offset 0, 16 bytes: "SQLite format 3" followed by a zero byte
head -c 15 -- main.db; echo
echo "nul=$(byte 15 1)"

# offset 16, 2 bytes: page size, big-endian, 1 means 65536
hi=$(byte 16 1); lo=$(byte 17 1)
ps=$(( hi * 256 + lo ))
[ "$ps" -eq 1 ] && ps=65536
echo "pagesize_in_range=$( [ "$ps" -ge 512 ] && [ "$ps" -le 65536 ] && echo yes || echo no )"
echo "pagesize_pow2=$( [ $(( ps & (ps - 1) )) -eq 0 ] && echo yes || echo no )"
prag=$($S -init /dev/null -batch main.db "PRAGMA page_size;")
echo "pagesize_matches_pragma=$( [ "$ps" = "$prag" ] && echo yes || echo no )"

# offsets 18 and 19: file format write and read version
echo "versions=$(byte 18 2)"

# offset 56, 4 bytes: text encoding, 1 means UTF-8
echo "encoding=$(byte 56 4)"
echo "pragma_encoding=$($S -init /dev/null -batch main.db 'PRAGMA encoding;')"

# the file is a whole number of pages
sz=$(wc -c < main.db)
echo "size_is_whole_pages=$( [ $(( sz % ps )) -eq 0 ] && echo yes || echo no )"
echo "at_least_one_page=$( [ "$sz" -ge "$ps" ] && echo yes || echo no )"
