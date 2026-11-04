/* PROFILE.md section 1: EXPLAIN and EXPLAIN QUERY PLAN must parse and execute
   without error, and lang_explain.html says either form makes the statement
   "behave as a query ... about how the SQL statement WOULD HAVE operated", so
   the explained statement is not carried out. TEST_INTERFACE.md section 6 puts
   the result rows out of scope, so this case steps through them without
   looking at any value. */
#include <stdio.h>
#include "sqlite3.h"

static int drain(sqlite3 *db, const char *sql) {
  sqlite3_stmt *st = 0;
  int rc;
  if (sqlite3_prepare_v2(db, sql, -1, &st, 0) != SQLITE_OK) return 0;
  while ((rc = sqlite3_step(st)) == SQLITE_ROW) { }
  if (rc != SQLITE_DONE) { sqlite3_finalize(st); return 0; }
  return sqlite3_finalize(st) == SQLITE_OK;
}

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  if (sqlite3_exec(db, "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT);"
                       "INSERT INTO t VALUES(1,'x'),(2,'y');"
                       "CREATE INDEX i1 ON t(b);", 0, 0, 0) != SQLITE_OK) return 2;

  printf("explain_select=%d\n", drain(db, "EXPLAIN SELECT a FROM t WHERE b = 'x'"));
  printf("explain_query_plan=%d\n",
         drain(db, "EXPLAIN QUERY PLAN SELECT a FROM t WHERE b = 'x'"));
  printf("explain_insert=%d\n", drain(db, "EXPLAIN INSERT INTO t VALUES(3,'z')"));
  printf("plain_select=%d\n", drain(db, "SELECT a FROM t ORDER BY a"));

  /* the explained INSERT was described, not performed */
  if (sqlite3_prepare_v2(db, "SELECT count(*) FROM t", -1, &st, 0) != SQLITE_OK) return 3;
  if (sqlite3_step(st) != SQLITE_ROW) return 4;
  printf("rows=%d\n", sqlite3_column_int(st, 0));
  sqlite3_finalize(st);

  sqlite3_close(db);
  return 0;
}
