/* Gate, boundary 2 of 2: the same two things through the static library --
   json_extract() returns its value and an index on an expression can be
   created and then queried. */
#include <stdio.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  int rc;

  if (sqlite3_open("gate.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db, "CREATE TABLE g(id INTEGER PRIMARY KEY, body TEXT);"
                       "INSERT INTO g VALUES(1,'{\"a\":42}'),(2,'{\"a\":7}');",
                   0, 0, 0) != SQLITE_OK) return 2;
  if (sqlite3_exec(db, "CREATE INDEX g_a ON g(json_extract(body,'$.a'));",
                   0, 0, 0) != SQLITE_OK) return 3;
  if (sqlite3_prepare_v2(db,
        "SELECT id, json_extract(body,'$.a') FROM g"
        " ORDER BY json_extract(body,'$.a')", -1, &st, 0) != SQLITE_OK) return 4;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    printf("%d|%d\n", sqlite3_column_int(st, 0), sqlite3_column_int(st, 1));
  }
  if (rc != SQLITE_DONE) { sqlite3_finalize(st); return 5; }
  if (sqlite3_finalize(st) != SQLITE_OK) return 6;
  sqlite3_close(db);
  return 0;
}
