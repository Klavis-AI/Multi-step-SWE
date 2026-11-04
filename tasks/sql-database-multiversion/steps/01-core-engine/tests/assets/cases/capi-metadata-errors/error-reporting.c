/* Failures are reported through the documented result codes; the message text
   itself is never compared. */
#include <stdio.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  int rc;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  /* c3ref/errcode.html defines sqlite3_errcode() only for the most recent
     FAILED call, so nothing is asserted about it before one has failed. */
  sqlite3_exec(db, "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT UNIQUE NOT NULL);"
                   "INSERT INTO t VALUES(1,'p');", 0, 0, 0);

  rc = sqlite3_prepare_v2(db, "SELECT bogus FROM t", -1, &st, 0);
  printf("prepare_ok=%d errcode_matches=%d msg_present=%d\n",
         rc == SQLITE_OK, sqlite3_errcode(db) == rc, sqlite3_errmsg(db) != 0);

  if (sqlite3_prepare_v2(db, "INSERT INTO t VALUES(2,'p')", -1, &st, 0) != SQLITE_OK) return 2;
  rc = sqlite3_step(st);
  printf("unique_step_constraint=%d\n", rc == SQLITE_CONSTRAINT);
  printf("finalize_reports_error=%d\n", sqlite3_finalize(st) != SQLITE_OK);

  if (sqlite3_prepare_v2(db, "INSERT INTO t VALUES(3,NULL)", -1, &st, 0) != SQLITE_OK) return 3;
  rc = sqlite3_step(st);
  printf("notnull_step_constraint=%d\n", rc == SQLITE_CONSTRAINT);
  sqlite3_finalize(st);

  /* the failed statements changed nothing */
  if (sqlite3_prepare_v2(db, "SELECT count(*) FROM t", -1, &st, 0) != SQLITE_OK) return 4;
  sqlite3_step(st);
  printf("rows=%d\n", sqlite3_column_int(st, 0));
  sqlite3_finalize(st);

  printf("close=%d\n", sqlite3_close(db) == SQLITE_OK);
  return 0;
}
