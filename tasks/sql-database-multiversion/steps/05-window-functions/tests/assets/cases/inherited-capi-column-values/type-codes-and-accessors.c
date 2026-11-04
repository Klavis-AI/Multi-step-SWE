/* c3ref/column_blob.html: sqlite3_column_type() reports the INITIAL storage
   class of the value, and its result is undefined once a converting accessor
   has run on the same column. Every type code below is therefore read into a
   local BEFORE any other accessor touches that column, so the case never
   depends on the order in which C evaluates printf's arguments. */
#include <stdio.h>
#include <string.h>
#include "sqlite3.h"

int main(void) {
  sqlite3 *db = 0;
  sqlite3_stmt *st = 0;
  const unsigned char *txt;
  const void *blb;
  int ty;
  int rc;

  if (sqlite3_open("main.db", &db) != SQLITE_OK) return 1;
  sqlite3_exec(db,
      "CREATE TABLE t(k INTEGER PRIMARY KEY, v);"
      "INSERT INTO t VALUES(1, 17);"
      "INSERT INTO t VALUES(2, 2.5);"
      "INSERT INTO t VALUES(3, 'abc');"
      "INSERT INTO t VALUES(4, x'00ff10');"
      "INSERT INTO t VALUES(5, NULL);", 0, 0, 0);

  if (sqlite3_prepare_v2(db, "SELECT v FROM t ORDER BY k", -1, &st, 0) != SQLITE_OK) return 2;

  /* row 1: INTEGER */
  rc = sqlite3_step(st);
  ty = sqlite3_column_type(st, 0);
  printf("r1 row=%d type=%d is_int=%d int=%d i64=%lld bytes=%d\n",
         rc == SQLITE_ROW, ty, ty == SQLITE_INTEGER,
         sqlite3_column_int(st, 0), (long long)sqlite3_column_int64(st, 0),
         sqlite3_column_bytes(st, 0));

  /* row 2: FLOAT */
  sqlite3_step(st);
  ty = sqlite3_column_type(st, 0);
  printf("r2 type=%d is_float=%d double=%.1f int=%d\n",
         ty, ty == SQLITE_FLOAT,
         sqlite3_column_double(st, 0), sqlite3_column_int(st, 0));

  /* row 3: TEXT */
  sqlite3_step(st);
  ty = sqlite3_column_type(st, 0);
  txt = sqlite3_column_text(st, 0);
  printf("r3 type=%d is_text=%d text=%s bytes=%d terminated=%d\n",
         ty, ty == SQLITE_TEXT,
         txt, sqlite3_column_bytes(st, 0), txt[3] == 0);

  /* row 4: BLOB */
  sqlite3_step(st);
  ty = sqlite3_column_type(st, 0);
  blb = sqlite3_column_blob(st, 0);
  printf("r4 type=%d is_blob=%d bytes=%d content=%02x%02x%02x\n",
         ty, ty == SQLITE_BLOB,
         sqlite3_column_bytes(st, 0),
         ((const unsigned char *)blb)[0], ((const unsigned char *)blb)[1],
         ((const unsigned char *)blb)[2]);

  /* row 5: NULL, with the documented conversions */
  sqlite3_step(st);
  ty = sqlite3_column_type(st, 0);
  printf("r5 type=%d is_null=%d int=%d double=%.1f text_null=%d blob_null=%d bytes=%d\n",
         ty, ty == SQLITE_NULL,
         sqlite3_column_int(st, 0), sqlite3_column_double(st, 0),
         sqlite3_column_text(st, 0) == 0, sqlite3_column_blob(st, 0) == 0,
         sqlite3_column_bytes(st, 0));

  rc = sqlite3_step(st);
  printf("done=%d\n", rc == SQLITE_DONE);
  sqlite3_finalize(st);
  sqlite3_close(db);
  return 0;
}
