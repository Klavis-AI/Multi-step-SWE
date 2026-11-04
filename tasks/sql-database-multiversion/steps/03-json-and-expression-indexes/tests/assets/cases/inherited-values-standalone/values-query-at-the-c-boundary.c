/* TEST_INTERFACE.md section 7: a VALUES statement is an ordinary statement at
   the library boundary too - sqlite3_prepare_v2() compiles one at a time and
   leaves *pzTail after its semicolon, a VALUES query reports its column count
   through sqlite3_column_count() and its rows through sqlite3_step(), and a
   VALUES statement the profile says must be rejected returns something other
   than SQLITE_OK. Section 3.3: for a failing call the verifier compares only
   "not SQLITE_OK", never a particular code and never the message. */
#include <stdio.h>
#include "sqlite3.h"

/* 1 when the statement did not compile, 0 when it did. */
static int rejected(sqlite3 *db, const char *sql) {
  sqlite3_stmt *st = 0;
  int rc = sqlite3_prepare_v2(db, sql, -1, &st, 0);
  if (st) sqlite3_finalize(st);
  return rc != SQLITE_OK;
}

static int count_rows(sqlite3 *db) {
  sqlite3_stmt *st = 0;
  int n = -1;
  if (sqlite3_prepare_v2(db, "SELECT count(*) FROM t", -1, &st, 0) != SQLITE_OK) return -1;
  if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return n;
}

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  const char *tail = 0;
  int rc, n;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db, "CREATE TABLE t(a INTEGER, b TEXT);"
                       "INSERT INTO t VALUES(1,'p');", 0, 0, 0) != SQLITE_OK) return 2;

  /* A multi-row VALUES statement: one statement, column count, rows in the
     order written (PROFILE.md section 2.2, "Row order"). */
  if (sqlite3_prepare_v2(db, "VALUES(1,'a'),(2,'b'); VALUES(3,'c');", -1, &st, &tail)
      != SQLITE_OK) return 3;
  printf("columns=%d\n", sqlite3_column_count(st));
  printf("tail=%s\n", tail);
  n = 0;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
    n++;
    printf("row %d %s\n", sqlite3_column_int(st, 0), sqlite3_column_text(st, 1));
  }
  printf("rows=%d done=%d\n", n, rc == SQLITE_DONE);
  printf("finalize=%d\n", sqlite3_finalize(st) == SQLITE_OK);

  /* The same query wrapped in a FROM subquery still reports two columns. */
  if (sqlite3_prepare_v2(db, "SELECT * FROM (VALUES(4,'d'),(5,'e')) ORDER BY 1",
                         -1, &st, 0) != SQLITE_OK) return 4;
  printf("subquery_columns=%d\n", sqlite3_column_count(st));
  sqlite3_finalize(st);

  /* The rejections PROFILE.md section 2.2 lists, at this boundary. */
  printf("reject_order_by=%d\n", rejected(db, "VALUES(1),(2) ORDER BY 1"));
  printf("reject_limit=%d\n",    rejected(db, "VALUES(1),(2) LIMIT 1"));
  printf("reject_width=%d\n",    rejected(db, "VALUES(1,2),(3)"));
  printf("reject_compound_width=%d\n",
         rejected(db, "VALUES(1,'a'),(2,'b') UNION ALL SELECT 3"));

  /* A rejected VALUES-sourced INSERT leaves the table exactly as it was. */
  printf("rows_before=%d\n", count_rows(db));
  printf("reject_insert=%d\n",
         sqlite3_exec(db, "INSERT INTO t SELECT * FROM (VALUES(9,'z'),(8));", 0, 0, 0)
         != SQLITE_OK);
  printf("rows_after=%d\n", count_rows(db));

  sqlite3_close(db);
  return 0;
}
