/* Gate, boundary 2 of 2: the static library answers a WITH query and a VALUES
   query through the documented C interface. */
#include <stdio.h>
#include "sqlite3.h"

static int run(sqlite3 *db, const char *sql) {
  sqlite3_stmt *st = 0;
  int rc;
  if (sqlite3_prepare_v2(db, sql, -1, &st, 0) != SQLITE_OK) return 1;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    printf("%d|%s\n", sqlite3_column_int(st, 0), sqlite3_column_text(st, 1));
  }
  if (rc != SQLITE_DONE) { sqlite3_finalize(st); return 1; }
  return sqlite3_finalize(st) == SQLITE_OK ? 0 : 1;
}

int main(void) {
  sqlite3 *db = 0;
  if (sqlite3_open("gate.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db, "CREATE TABLE g(a INTEGER PRIMARY KEY, b TEXT);"
                       "INSERT INTO g VALUES(1,'ok'),(2,'two');", 0, 0, 0) != SQLITE_OK) return 2;
  if (run(db, "WITH c(k,v) AS (SELECT a, b FROM g) SELECT k, v FROM c ORDER BY k")) return 3;
  if (run(db, "VALUES(3,'three')")) return 4;
  if (run(db, "SELECT * FROM (VALUES(4,'four'),(5,'five')) ORDER BY 1")) return 5;
  sqlite3_close(db);
  return 0;
}
