-- R05.15: a JSONB value inserted into a column is stored as a blob and comes
-- back out of the table byte for byte.
-- R05.7: json() of the value read back from the column is the JSON text that
-- was handed to jsonb() in the first place.
--
-- typeof() and length() describe what the column holds, json() and the two
-- accessors read it after the round trip, and hex() prints the stored bytes.
-- Everything here is this engine reading its own storage; R05.8's case is
-- where stock sqlite3 is asked the same questions about the same file.
CREATE TABLE d(id INTEGER PRIMARY KEY, doc BLOB);
INSERT INTO d VALUES(1,jsonb('{"a":1,"b":"two"}')),(2,jsonb('[1,2,3]'));
SELECT id, typeof(doc), length(doc) FROM d ORDER BY id;
SELECT id, json(doc) FROM d ORDER BY id;
SELECT json_extract(doc,'$.b') FROM d WHERE id=1;
SELECT json_array_length(doc) FROM d WHERE id=2;
SELECT hex(doc) FROM d WHERE id=2;
