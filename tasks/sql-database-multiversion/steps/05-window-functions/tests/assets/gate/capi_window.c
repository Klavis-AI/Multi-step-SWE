/* Gate, boundary 2 of 2: the same two things through the static library --
   one SELECT whose result column carries row_number() OVER (ORDER BY ...),
   compiled with sqlite3_prepare_v2() and read row by row with sqlite3_step(),
   and one ALTER TABLE ... RENAME COLUMN whose new name a following query uses.

   TEST_INTERFACE.md 7: a SELECT carrying window functions is an ordinary query
   at this boundary and RENAME COLUMN is ordinary DDL; no new C entry point is
   required for either, and section 7 names exactly these two as the gate.
   PROFILE.md 5.4 fixes row_number() as the position in the window ORDER BY's
   order, 5.2 fixes the output order through the outer SELECT's own ORDER BY,
   and 5.9 fixes what RENAME COLUMN does. */
#include <stdio.h>
#include "sqlite3.h"

static sqlite3 *db;

/* Runs one two-integer-column query and prints its rows. */
static int dump2(const char *sql) {
  sqlite3_stmt *st = 0;
  int rc;
  if (sqlite3_prepare_v2(db, sql, -1, &st, 0) != SQLITE_OK) return 1;
  if (sqlite3_column_count(st) != 2) { sqlite3_finalize(st); return 1; }
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    printf("%d|%d\n", sqlite3_column_int(st, 0), sqlite3_column_int(st, 1));
  }
  if (rc != SQLITE_DONE) { sqlite3_finalize(st); return 1; }
  return sqlite3_finalize(st) != SQLITE_OK;
}

int main(void) {
  if (sqlite3_open("gate.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db, "CREATE TABLE g(k INTEGER PRIMARY KEY, v INTEGER UNIQUE);"
                       "INSERT INTO g VALUES(1,30),(2,10),(3,20);",
                   0, 0, 0) != SQLITE_OK) return 2;

  if (dump2("SELECT k,"
            "       row_number() OVER (ORDER BY v) AS rn"
            "  FROM g ORDER BY k")) return 3;

  if (sqlite3_exec(db, "ALTER TABLE g RENAME COLUMN v TO amount;",
                   0, 0, 0) != SQLITE_OK) return 4;

  if (dump2("SELECT k, amount FROM g ORDER BY k")) return 5;

  sqlite3_close(db);
  return 0;
}
