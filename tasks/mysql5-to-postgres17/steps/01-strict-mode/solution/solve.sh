#!/usr/bin/env bash
#
# Reference solution for step 01 -- "MySQL stops repairing invalid values
# silently".
#
# The server is put into STRICT_TRANS_TABLES and the five statements that were
# relying on it to invent a value are made to supply that value themselves.
# Every value written here is the one the permissive server wrote for the same
# input; none of it was chosen. What each statement used to produce is recorded
# in NOTES-authoring.md §19.5 and in the step's fixture, and the point of the
# step is that the recorded bytes do not move.
#
# Nothing here widens a column, changes a charset, drops a table or relaxes the
# mode again for a single connection. Those are later steps' work, and doing
# them here would buy this step's must-flip probes without answering its
# question.
set -euo pipefail

APP=/app
MY_CNF=/etc/mysql/my.cnf
STRICT='STRICT_TRANS_TABLES,NO_ENGINE_SUBSTITUTION'

echo "== step 01: the server stops repairing invalid values silently"

# Every edit is anchored on bytes that exist exactly as many times as the edit
# expects, and the script dies if they do not. A migration script that silently
# edits nothing is the failure mode worth spending ten lines on.
python3 - "$APP" "$MY_CNF" "$STRICT" <<'PY'
import io, sys

app, my_cnf, strict = sys.argv[1:4]


def edit(path, old, new, count=1):
    with io.open(path, "r", encoding="utf-8") as fh:
        before = fh.read()
    if before.count(old) == 0 and before.count(new) == count:
        print("   already applied  %s" % path)
        return
    found = before.count(old)
    if found != count:
        raise SystemExit("%s: anchor occurs %d time(s), expected %d -- "
                         "refusing to guess" % (path, found, count))
    after = before.replace(old, new)
    with io.open(path, "w", encoding="utf-8") as fh:
        fh.write(after)
    print("   patched %d site(s)  %s" % (count, path))


# 1. The server. STRICT_TRANS_TABLES is the mode that makes a transactional
#    table refuse a value it cannot store instead of adjusting it; the existing
#    NO_ENGINE_SUBSTITUTION stays, because dropping it would change what
#    CREATE TABLE does with an unknown engine and that is not this step.
edit(my_cnf,
     "sql_mode                        = NO_ENGINE_SUBSTITUTION",
     "sql_mode                        = " + strict)

# 2. jf_session.CurrentRequest is `varchar(1024) NOT NULL`, and the session
#    bookkeeping UPDATE binds `jf::$Request`, a static nothing ever assigns --
#    so it binds PHP null on every request the application serves. The server
#    stored the empty string for it; the application now says so.
edit(app + "/_japp/model/lib/session.php",
     "jf::time (), jf::$Request,session_id ()",
     "jf::time (), (string) jf::$Request, session_id ()")

# 3. jf_xuser has ten columns, nine of them NOT NULL with no DEFAULT, and both
#    INSERTs name four. The server filled the other six with the implicit
#    defaults for their types; those values are now in the statement. They are
#    literals rather than bound parameters because they are not variable: a new
#    extended user has no reset password, has never logged in, has no failed
#    attempts, is not locked out and is not yet activated.
edit(app + "/_japp/model/lib/xuser.php",
     "(ID,Email,CreateTimestamp,PasswordChangeTimestamp) VALUES (?,?,?,?)",
     "(ID,Email,CreateTimestamp,PasswordChangeTimestamp,"
     "TemporaryResetPassword,TemporaryResetPasswordTimeout,"
     "LastLoginTimestamp,FailedLoginAttempts,LockTimeout,Activated)"
     " VALUES (?,?,?,?,'',0,0,0,0,0)",
     count=2)

# 4. contest_challenges.Points is `int NOT NULL` and the admin form posts an
#    empty string when the field is left blank. The server read that as 0.
edit(app + "/app/control/mode/contest/ajax/challenge.php",
     "'Points' => $_POST['points'],",
     "'Points' => (int) $_POST['points'],")

# 5. contest_challenges is DEFAULT CHARSET=latin1 and this connection speaks
#    utf8 (jf\db\adapter\mysqli::$Charset), so every string bound here used to
#    be narrowed by the server on the way in: one `?` per character latin1 has
#    no room for, one `?` per byte that was not valid UTF-8 to begin with. A
#    server that refuses the row instead of narrowing it has not given the
#    column a wider repertoire, so the narrowing moves into the application.
#    MySQL's `latin1` is CP1252, not ISO-8859-1 -- the 0x80-0x9F range holds
#    EUR, the dashes and the curly quotes -- and mbstring's CP1252 with its
#    default substitute character reproduces the server's mapping exactly.
edit(app + "/app/model/contest/challenges.php",
     """        $keys = array_keys($array);
        $values = array_values($array);""",
     """        $keys = array_keys($array);
        $values = array_map(array(__CLASS__, 'narrowToColumnCharset'),
                            array_values($array));""")

edit(app + "/app/model/contest/challenges.php",
     """    public static function add($array = null)
    {""",
     """    /**
     * Narrow a value to what this table's columns can actually hold.
     *
     * Every text column of contest_challenges is latin1 (the table is
     * DEFAULT CHARSET=latin1) while the connection is utf8, so the server
     * used to fold the value on the way in and substitute `?` for whatever
     * would not fit. MySQL's latin1 is CP1252; the result is converted back
     * to UTF-8 because that is the encoding the connection carries.
     *
     * @param mixed $value one bound parameter
     * @return mixed the same value, narrowed if it was text
     */
    private static function narrowToColumnCharset($value)
    {
        if (!is_string($value)) {
            return $value;
        }

        return mb_convert_encoding(
            mb_convert_encoding($value, 'CP1252', 'UTF-8'),
            'UTF-8',
            'CP1252'
        );
    }

    public static function add($array = null)
    {""")

# 6. lesson_XSS2_messages.message is latin1 too -- the lesson's table comes
#    from SQL/webgoat.sql, where the whole schema was created latin1 -- and the
#    lesson posts the message body straight into it. Same narrowing, same
#    reason. (The lesson's own reset() recreates the table with no CHARSET
#    clause and so brings it back as the database default; that is a separate
#    defect and belongs to the step that gives this column a UTF-8 repertoire,
#    not to this one.)
edit(app + "/challenges/single/XSS2/index.php",
     """    private function addMessage($message)
    {
        \\jf::SQL("INSERT INTO ".self::TABLE_NAME."(message) VALUES (?)", $message);
    }""",
     """    private function addMessage($message)
    {
        // The column is latin1 and the connection is utf8, so the server used
        // to narrow this value and substitute `?` for whatever would not fit.
        // It no longer does, so the lesson does it. MySQL's latin1 is CP1252.
        $message = mb_convert_encoding(
            mb_convert_encoding($message, 'CP1252', 'UTF-8'),
            'UTF-8',
            'CP1252'
        );

        \\jf::SQL("INSERT INTO ".self::TABLE_NAME."(message) VALUES (?)", $message);
    }""")
PY

echo "== restarting MySQL so the new sql_mode takes effect"
/etc/init.d/mysql restart >/dev/null 2>&1 || /etc/init.d/mysql start >/dev/null 2>&1
for _ in $(seq 1 90); do
	mysqladmin --socket=/var/run/mysqld/mysqld.sock ping >/dev/null 2>&1 && break
	sleep 1
done

# Assert the change landed where it counts: in the running server, not just in
# the file. A configuration edit that the server never read is the quietest way
# for this step to look done and not be.
running=$(mysql --socket=/var/run/mysqld/mysqld.sock -uroot -N -B \
	-e "SELECT @@GLOBAL.sql_mode")
echo "   @@GLOBAL.sql_mode = $running"
case ",$running," in
*,STRICT_TRANS_TABLES,* | *,STRICT_ALL_TABLES,*) ;;
*)
	echo "the running server is not in strict mode" >&2
	exit 1
	;;
esac

php -l "$APP/_japp/model/lib/session.php" >/dev/null
php -l "$APP/_japp/model/lib/xuser.php" >/dev/null
php -l "$APP/app/control/mode/contest/ajax/challenge.php" >/dev/null
php -l "$APP/app/model/contest/challenges.php" >/dev/null
php -l "$APP/challenges/single/XSS2/index.php" >/dev/null
echo "   the five edited PHP files parse"

echo "== done"
