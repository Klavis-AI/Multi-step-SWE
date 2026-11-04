/* A prepare that fails returns a non-OK code and sets *ppStmt to NULL;
   so does input that contains no SQL at all. */
#include <stdio.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = (sqlite3_stmt *)1;
  int rc;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  sqlite3_exec(db, "CREATE TABLE t(a INTEGER PRIMARY KEY);", 0, 0, 0);

  rc = sqlite3_prepare_v2(db, "SELECT * FROM no_such_table", -1, &st, 0);
  printf("bad_table_ok=%d stmt_null=%d\n", rc == SQLITE_OK, st == 0);

  st = (sqlite3_stmt *)1;
  rc = sqlite3_prepare_v2(db, "NOT VALID SQL", -1, &st, 0);
  printf("syntax_ok=%d stmt_null=%d\n", rc == SQLITE_OK, st == 0);

  st = (sqlite3_stmt *)1;
  rc = sqlite3_prepare_v2(db, "  -- only a comment\n", -1, &st, 0);
  printf("comment_ok=%d stmt_null=%d\n", rc == SQLITE_OK, st == 0);

  /* nByte limits how much of the buffer is read */
  if (sqlite3_prepare_v2(db, "SELECT 42 xxxxxx", 9, &st, 0) != SQLITE_OK) return 2;
  rc = sqlite3_step(st);
  printf("step=%d value=%d\n", rc == SQLITE_ROW, sqlite3_column_int(st, 0));
  sqlite3_finalize(st);

  sqlite3_close(db);
  return 0;
}
