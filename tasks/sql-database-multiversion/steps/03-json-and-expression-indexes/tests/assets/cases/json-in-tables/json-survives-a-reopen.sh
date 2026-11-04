# json1.html 2.0: "The json1 extension (currently) stores JSON as ordinary
# text", so a committed JSON document must survive a close and reopen exactly
# as any other TEXT value does (PROFILE.md 7, TEST_INTERFACE.md 4).  Each
# invocation below is a separate process started after the previous one
# exited, and runs under the start-up defaults of TEST_INTERFACE.md 2.2.
set -u
S="$SQLITE3"
$S -init /dev/null -batch j.db \
  "CREATE TABLE d(id INTEGER PRIMARY KEY, body TEXT);
   INSERT INTO d VALUES(1, json_object('k','v','n',json_array(1,2)));
   INSERT INTO d VALUES(2, json('  {\"z\" : [ 3 ] }  '));" || exit 1
$S -init /dev/null -batch j.db "SELECT id, body FROM d ORDER BY id;" || exit 1
$S -init /dev/null -batch j.db \
  "SELECT json_extract(body,'\$.n[1]') FROM d WHERE id=1;
   SELECT json_extract(body,'\$.z[0]') FROM d WHERE id=2;
   SELECT count(*) FROM d WHERE json_valid(body);
   PRAGMA integrity_check;" || exit 1
# a JSON document written by one process can be edited by the next
$S -init /dev/null -batch j.db "UPDATE d SET body = json_set(body,'\$.k','w') WHERE id=1;" || exit 1
$S -init /dev/null -batch j.db "SELECT body FROM d WHERE id=1;" || exit 1
