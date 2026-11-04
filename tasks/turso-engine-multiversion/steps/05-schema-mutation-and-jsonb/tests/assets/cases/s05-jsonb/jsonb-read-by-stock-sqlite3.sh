# R05.8: a JSONB blob this engine writes into a file is read back by stock
# sqlite3 3.50.2 -- the same JSON text, the same path answers, the same bytes.
#
# The engine writes three documents of different shapes and exits. Everything
# after that is the stock binary: json() prints each document, json_extract,
# json_array_length and json_type walk into object keys, array subscripts and a
# nested path, and the last query re-encodes what it just decoded and compares
# the hex to the bytes on disk. That last line is the differential this case
# carries in place of the automatic one -- a .sh case is not run through stock
# sqlite3 a second time by the harness, so the second opinion has to be inside
# it. It prints 1 for each row only if the engine's encoding of the document
# and the stock binary's encoding of the same document are byte-for-byte
# equal, which is a stronger claim than "both can read it".
#
# typeof(doc) is asserted as blob: a reader that has never seen this engine
# must find an ordinary blob in the column, not a text spelling of the JSON.
#
# The writing invocation ends by reading back the table it wrote. A conforming
# engine does not need that: spec/PROFILE.md's durability rule requires a
# process that writes and then exits to leave its committed data recoverable by
# the next reader that opens the files. The read-back is this case's own belt
# and braces, its output is discarded, and nothing below depends on what it
# printed.
set -u
"$CLI" -m "$CLI_MODE" -q main.db > /dev/null 2>&1 <<'SQL'
CREATE TABLE d(id INTEGER PRIMARY KEY, doc BLOB);
INSERT INTO d VALUES
  (1, jsonb('{"a":1,"b":"two","c":[3,4]}')),
  (2, jsonb('[true,false,null]')),
  (3, jsonb('{"n":{"deep":[1,2,{"k":"v"}]}}'));
SELECT count(*) FROM d;
SQL
"$SQLITE3" -batch -list -noheader -separator '|' main.db 2>&1 <<'SQL'
SELECT id, typeof(doc), json(doc) FROM d ORDER BY id;
SELECT json_extract(doc,'$.b'), json_array_length(doc,'$.c') FROM d WHERE id=1;
SELECT json_type(doc,'$[0]'), json_type(doc,'$[2]') FROM d WHERE id=2;
SELECT json_extract(doc,'$.n.deep[2].k') FROM d WHERE id=3;
SELECT id, hex(doc) = hex(jsonb(json(doc))) FROM d ORDER BY id;
SQL
