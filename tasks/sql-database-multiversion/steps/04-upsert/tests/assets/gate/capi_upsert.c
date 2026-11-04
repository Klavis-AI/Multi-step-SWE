/* Gate, boundary 2 of 2: the same thing through the static library -- one
   INSERT ... ON CONFLICT (target) DO UPDATE SET that updates the row it
   collides with, leaves the other rows alone, and inserts a row that collides
   with nothing. */
#include <stdio.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  int rc;

  if (sqlite3_open("gate.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db, "CREATE TABLE g(k INTEGER PRIMARY KEY, c TEXT);"
                       "INSERT INTO g VALUES(1,'one'),(2,'two');",
                   0, 0, 0) != SQLITE_OK) return 2;
  if (sqlite3_exec(db, "INSERT INTO g(k,c) VALUES(2,'TWO'),(3,'three')"
                       " ON CONFLICT(k) DO UPDATE SET c=excluded.c;",
                   0, 0, 0) != SQLITE_OK) return 3;
  if (sqlite3_prepare_v2(db, "SELECT k,c FROM g ORDER BY k", -1, &st, 0)
      != SQLITE_OK) return 4;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    printf("%d|%s\n", sqlite3_column_int(st, 0),
           (const char *)sqlite3_column_text(st, 1));
  }
  if (rc != SQLITE_DONE) { sqlite3_finalize(st); return 5; }
  if (sqlite3_finalize(st) != SQLITE_OK) return 6;
  sqlite3_close(db);
  return 0;
}
