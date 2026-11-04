/* TEST_INTERFACE.md 7: a statement the profile says must be rejected fails
   the same way any other rejected statement does -- a non-SQLITE_OK return
   from the library, with the message wording not scored.  These are the
   rejections of json1.html 2.1 (malformed JSON), 2.2 (malformed path), 3.2
   and 3.6 (a BLOB argument to json_array()/json_object()) and 3.5 (an even
   number of arguments), observed at the C boundary rather than the CLI.

   Only "did not return SQLITE_OK" is compared, as TEST_INTERFACE.md 3.3
   allows; no specific result code is asserted. */
#include <stdio.h>
#include "sqlite3.h"

static sqlite3 *db;

static void rejects(const char *label, const char *sql) {
  char *msg = 0;
  int rc = sqlite3_exec(db, sql, 0, 0, &msg);
  printf("%s|%d\n", label, rc != SQLITE_OK);
  if (msg) sqlite3_free(msg);
}

static void accepts(const char *label, const char *sql) {
  int rc = sqlite3_exec(db, sql, 0, 0, 0);
  printf("%s|%d\n", label, rc == SQLITE_OK);
}

int main(void) {
  sqlite3_stmt *st = 0;
  if (sqlite3_open("j.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db, "CREATE TABLE d(id INTEGER PRIMARY KEY, body TEXT);"
                       "INSERT INTO d VALUES(1,'{\"a\":1}');", 0, 0, 0) != SQLITE_OK)
    return 2;

  accepts("ok_json",   "SELECT json('{\"a\":1}')");
  rejects("bad_json",  "SELECT json('nope')");
  rejects("bad_json2", "SELECT json_type('[1,')");
  rejects("bad_path",  "SELECT json_extract('{\"a\":1}','a')");
  rejects("bad_path2", "SELECT json_set('{}','x',1)");
  rejects("blob_arr",  "SELECT json_array(x'41')");
  rejects("blob_obj",  "SELECT json_object('a',x'41')");
  rejects("blob_val",  "SELECT json_set('{}','$.a',x'41')");
  rejects("even_args", "SELECT json_set('{\"a\":1}','$.a')");
  accepts("ok_after",  "SELECT json_extract('{\"a\":1}','$.a')");

  /* A rejected UPDATE leaves the stored document exactly as it was. */
  rejects("bad_update", "UPDATE d SET body = json_set(body,'zz',1)");
  st = 0;
  if (sqlite3_prepare_v2(db, "SELECT body FROM d WHERE id=1", -1, &st, 0) != SQLITE_OK)
    return 3;
  if (sqlite3_step(st) != SQLITE_ROW) return 4;
  printf("unchanged|%s\n", (const char *)sqlite3_column_text(st, 0));
  sqlite3_finalize(st);

  sqlite3_close(db);
  return 0;
}
