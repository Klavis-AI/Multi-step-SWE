/* Gate, boundary 2 of 2: the static library creates a database through the
   documented C interface, stores a row, reads it back, and a second
   connection opened on the same file still sees it. */
#include <stdio.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  int rc;

  if (sqlite3_open("gate.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db, "CREATE TABLE g(a INTEGER PRIMARY KEY, b TEXT);"
                       "INSERT INTO g VALUES(1,'ok');", 0, 0, 0) != SQLITE_OK) return 2;
  if (sqlite3_prepare_v2(db, "SELECT a, b FROM g ORDER BY a", -1, &st, 0) != SQLITE_OK) return 3;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    printf("%d|%s\n", sqlite3_column_int(st, 0), sqlite3_column_text(st, 1));
  }
  if (rc != SQLITE_DONE) return 4;
  if (sqlite3_finalize(st) != SQLITE_OK) return 5;
  if (sqlite3_close(db) != SQLITE_OK) return 6;

  db = 0;
  if (sqlite3_open("gate.db", &db) != SQLITE_OK) return 7;
  if (sqlite3_prepare_v2(db, "SELECT count(*) FROM g", -1, &st, 0) != SQLITE_OK) return 8;
  if (sqlite3_step(st) != SQLITE_ROW) return 9;
  printf("reopened=%d\n", sqlite3_column_int(st, 0));
  sqlite3_finalize(st);
  sqlite3_close(db);
  return 0;
}
