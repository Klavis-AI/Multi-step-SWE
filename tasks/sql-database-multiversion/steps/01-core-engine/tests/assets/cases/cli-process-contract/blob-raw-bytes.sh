set -u
# TEST_INTERFACE.md section 2.3: in list mode "BLOB values [are written] as
# their raw bytes" - not as a hex string, not as an x'..' literal and not as a
# placeholder. The bytes are read back through od so the comparison itself
# stays printable ASCII; length() shows the value really is a BLOB of that
# many bytes rather than the text of a hex rendering.
S="$SQLITE3"
$S -init /dev/null -batch main.db \
  "CREATE TABLE b(k INTEGER PRIMARY KEY, v BLOB);
   INSERT INTO b VALUES(1, x'48656c6c6f');
   INSERT INTO b VALUES(2, x'e28692');
   INSERT INTO b VALUES(3, x'ff7f01');" || exit 1

# a blob whose bytes happen to be printable ASCII comes out as those characters
printf 'SELECT v FROM b WHERE k = 1;\n' | $S -init /dev/null -batch main.db

# every byte, including the non-ASCII ones, reaches standard output unaltered
printf 'SELECT k, v, length(v) FROM b ORDER BY k;\n' \
  | $S -init /dev/null -batch main.db \
  | od -An -v -tx1 | tr -d ' \n'
echo

# typeof() confirms the stored values are BLOBs and hex() renders the same bytes
printf "SELECT k, typeof(v), hex(v) FROM b ORDER BY k;\n" \
  | $S -init /dev/null -batch main.db
