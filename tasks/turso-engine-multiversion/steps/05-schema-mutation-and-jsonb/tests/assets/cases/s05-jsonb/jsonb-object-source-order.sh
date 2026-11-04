# R05.7 and R05.11: an object keeps the order its members were written in, in
# the rendering and in the bytes.
#
# TEST_INTERFACE.md, under "The JSONB element header byte, stated", fixes the
# OBJECT payload as label, value, label, value in source sequence and names
# this exact value: json(jsonb('{"z":1,"a":2}')) is {"z":1,"a":2} and never
# {"a":2,"z":1}. Every other object in this corpus is already in ascending
# key order, so without this case nothing in the suite can tell a
# source-order encoder from one that sorts labels. These statements are that
# assertion.
#
# The bytes are read back with stock sqlite3's own hex(), not the engine's.
# At this checkpoint the reference renders a byte above 0x7f through a UTF-8
# replacement sequence -- `hex(x'00ff10')` answers `00EFBFBD10`, measured in
# step 04's TEST_INTERFACE.md -- and R10.8 fixes that defect three checkpoints
# later. An OBJECT whose payload reaches eight bytes has `8C` in its header,
# so asking the engine's own hex() for these bytes would grade the defect
# rather than the encoding. Stock 3.50.2 is the reader R05.8 asks to confirm
# the bytes with anyway, and it reads the file this engine wrote.
#
# An encoder that sorted labels writes the "a" pair first, in the rendering
# and in the file, and fails the first difference on both.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE d(id INTEGER PRIMARY KEY, doc BLOB);
INSERT INTO d VALUES
  (1, jsonb('{"z":1,"a":2}')),
  (2, jsonb('{"z":{"y":1,"b":2},"a":[3]}'));
SELECT count(*) FROM d;
SQL
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
SELECT json(jsonb('{"z":1,"a":2}'));
SELECT json(jsonb('{"z":{"y":1,"b":2},"a":[3]}'));
SQL
echo "stock_bytes:"
"$SQLITE3" -batch -list -separator '|' -noheader main.db \
    'SELECT hex(doc) FROM d ORDER BY id;' 2>&1
