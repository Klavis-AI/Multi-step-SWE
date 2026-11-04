/* TEST_INTERFACE.md section 3.1 and 3.3: the two declared 64-bit integer
   typedefs, the fixed numeric values of the result-code and column-type
   macros, and sqlite3_busy_timeout as a declared, callable entry point.
   The values themselves are the ones the contract table fixes; the macros
   are used by name so a header that spells them differently does not
   compile. */
#include <stdio.h>
#include "sqlite3.h"
#include "sqlite3.h"   /* section 3: the header is idempotent (include guards) */

int main(void) {
  sqlite3 *db = 0;
  sqlite3_int64 s = -9007199254740993LL;   /* outside double precision */
  sqlite3_uint64 u = 18446744073709551615ULL;

  printf("codes=%d %d %d %d %d %d %d\n",
         SQLITE_OK, SQLITE_ERROR, SQLITE_BUSY, SQLITE_CONSTRAINT,
         SQLITE_MISUSE, SQLITE_ROW, SQLITE_DONE);
  printf("column_types=%d %d %d %d %d\n",
         SQLITE_INTEGER, SQLITE_FLOAT, SQLITE_TEXT, SQLITE_BLOB, SQLITE_NULL);
  printf("destructors=%d %d\n",
         SQLITE_STATIC == (sqlite3_destructor_type)0,
         SQLITE_TRANSIENT == (sqlite3_destructor_type)-1);

  printf("int64_width=%d uint64_width=%d\n",
         (int)sizeof(sqlite3_int64), (int)sizeof(sqlite3_uint64));
  printf("int64_value=%lld\n", (long long)s);
  printf("uint64_unsigned=%d\n", (u + 1) == 0);

  if (sqlite3_open(":memory:", &db) != SQLITE_OK) return 1;
  /* sqlite3_busy_timeout must be declared and callable. c3ref/busy_timeout.html
     states no return value for it, so none is compared: what is asserted is
     that the connection still works after a timeout is set and after it is
     turned off again (an argument <= 0 "turns off all busy handlers"). */
  sqlite3_busy_timeout(db, 250);
  printf("usable_after_timeout=%d\n",
         sqlite3_exec(db, "CREATE TABLE t(a INTEGER PRIMARY KEY);", 0, 0, 0) == SQLITE_OK);
  sqlite3_busy_timeout(db, 0);
  printf("usable_after_timeout_off=%d\n",
         sqlite3_exec(db, "INSERT INTO t VALUES(1);", 0, 0, 0) == SQLITE_OK);
  sqlite3_close(db);
  return 0;
}
