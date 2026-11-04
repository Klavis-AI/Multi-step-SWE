#!/usr/bin/env bash
#
# Reference solution for step 04 -- "the application talks to PostgreSQL".
#
# Steps 02 and 03 built the PostgreSQL schema and filled it. Nothing read it:
# every page still came out of MySQL. This step moves the application itself,
# and that turns out to be one move rather than two, because the SQL and the
# driver cannot be separated. `REPLACE INTO`, `GROUP_CONCAT(... SEPARATOR ...)`,
# `ALTER TABLE ... AUTO_INCREMENT` and a back-quoted identifier have no spelling
# that both servers accept, so there is no intermediate tree in which the
# queries have been rewritten and MySQL still answers them. Rewriting the
# queries *is* changing servers.
#
# Four things are delivered here, and the first three are forced by the
# framework rather than chosen:
#
#   * An adapter. `jf\DatabaseManager` loads
#     `_japp/model/lib/db/adapter/<name>.php` and expects the class
#     `DB_<name>` with a statement class beside it; the configuration names
#     the adapter. So a PostgreSQL deployment is a `pdo_pgsql.php` in that
#     directory, written to the contract the family's other members already
#     keep -- which is a contract of behaviour, not of signatures, and most of
#     this file's length is the difference between what mysqli returns and
#     what PDO_PgSQL returns for the same question.
#
#   * The queries. Sixty-eight edits over twenty-two files, each one asserting
#     how many occurrences it expected before it makes them. They are listed
#     by kind in the patch script below; none of them is cosmetic and none of
#     them is a preference. The ones that are not syntax are the interesting
#     ones: MySQL answers an unordered `LIMIT 1` from the primary-key index
#     and PostgreSQL from the heap, `LOWER()` folds by character set on one
#     server and by collation on the other, and every MySQL 5.7 collation
#     ignores trailing blanks in a comparison while no PostgreSQL collation
#     does.
#
#   * The configuration. One word in `app/config/application.php`, and the
#     removal of the ORM's request-time `updateSchema()` -- which exists to
#     make the entity metadata authoritative over the database, and is now
#     pointed at a migrated schema that `install/_db` owns.
#
#   * The MySQL-only source that is no longer part of this deployment: the
#     `mysqli` adapter, and the three vendored Doctrine DBAL modules for
#     databases this application does not use. A deployment that still ships
#     an Oracle schema manager and a SQL Server connection has not finished
#     moving, and each of those files is MySQL, Oracle or T-SQL text that no
#     PostgreSQL server would accept.
#
# What is *not* here is step 05's: `pdo_mysql.php` is still in the adapter
# directory, the two adapter self-tests still name mysqli, Doctrine's DBAL
# still has a `mysqli` connection class, and `install/_db/mysqli.*.sql` is
# still the MySQL installer. None of that runs in this deployment; removing it
# is the next step's leap, and MySQL is still installed and running here.
set -euo pipefail

APP=/app
DB=webgoatphp
PREFIX=jf_
PGPORT=5432
PGSOCK=/var/run/postgresql
MYSOCK=/var/run/mysqld/mysqld.sock

ADAPTERS=$APP/_japp/model/lib/db/adapter
DATA=$APP/install/_db/pdo_pgsql.data.sql

echo "== step 04: the application reads and writes PostgreSQL 17"

psu() { su -s /bin/sh postgres -c "$*"; }
psql_() { psu "psql -p $PGPORT -h $PGSOCK -d $DB -v ON_ERROR_STOP=1 $*"; }

SCRATCH=$(mktemp -d /tmp/wg-step04.XXXXXX)
trap 'rm -rf "$SCRATCH"' EXIT
chmod 755 "$SCRATCH"

# ----------------------------------------------------------- 1. both servers --
/usr/local/sbin/wg-services start
for i in $(seq 1 60); do
    mysql --socket="$MYSOCK" -uroot -e 'SELECT 1' >/dev/null 2>&1 && break
    sleep 1
done
for i in $(seq 1 60); do
    psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc 'SELECT 1'" >/dev/null 2>&1 && break
    sleep 1
done
psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc 'SELECT version()'"

# ------------------------------------------------------------- 2. the login --
# `app/config/application.php` says the application connects as `webgoat` with
# password `webgoat`. That was a MySQL account; PostgreSQL needs one of its own
# by the same name, because the configuration is a deliverable of this
# deployment and not something to be edited around.
#
# The privileges are the ones the application actually exercises. It does not
# only read: the lessons create and drop their own tables on reset, the stats
# plugin inserts on every page, and the session table is written on every
# request -- so the role owns the schema and everything in it rather than being
# granted a list that the next `CREATE TABLE` would fall outside of.
cat > "$SCRATCH/role.sql" <<'PGSQL'
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'webgoat') THEN
        ALTER ROLE webgoat LOGIN PASSWORD 'webgoat';
    ELSE
        CREATE ROLE webgoat LOGIN PASSWORD 'webgoat';
    END IF;
END $$;
ALTER ROLE webgoat SET search_path = public;
PGSQL
chmod 644 "$SCRATCH/role.sql"
psu "psql -p $PGPORT -h $PGSOCK -d postgres -q -v ON_ERROR_STOP=1 -f '$SCRATCH/role.sql'"
psu "psql -p $PGPORT -h $PGSOCK -d postgres -q -v ON_ERROR_STOP=1 \
     -c 'GRANT CONNECT ON DATABASE $DB TO webgoat'"

cat > "$SCRATCH/own.sql" <<'PGSQL'
ALTER SCHEMA public OWNER TO webgoat;
GRANT USAGE, CREATE ON SCHEMA public TO webgoat;
DO $$
DECLARE r record;
BEGIN
    FOR r IN SELECT format('ALTER TABLE public.%I OWNER TO webgoat', tablename) AS c
               FROM pg_tables WHERE schemaname = 'public'
    LOOP EXECUTE r.c; END LOOP;
    FOR r IN SELECT format('ALTER SEQUENCE public.%I OWNER TO webgoat', sequencename) AS c
               FROM pg_sequences WHERE schemaname = 'public'
    LOOP EXECUTE r.c; END LOOP;
END $$;
GRANT ALL ON ALL TABLES IN SCHEMA public TO webgoat;
GRANT ALL ON ALL SEQUENCES IN SCHEMA public TO webgoat;
PGSQL
chmod 644 "$SCRATCH/own.sql"
psql_ "-q -f '$SCRATCH/own.sql'"
echo "   role webgoat owns the public schema of $DB"

# The application reaches the server the way its configuration says -- host
# `localhost`, which PDO_PgSQL resolves to TCP -- so the host line has to be
# there. Appended only if some equivalent is not already present.
HBA=$(psu "psql -p $PGPORT -h $PGSOCK -d postgres -Atqc 'SHOW hba_file'")
if ! grep -Eq '^[[:space:]]*host[[:space:]]+all[[:space:]]+all[[:space:]]+127\.0\.0\.1/32' "$HBA"; then
    printf '%s\n' \
      "# the application connects as webgoat over TCP from this host" \
      "host    all   all   127.0.0.1/32  scram-sha-256" \
      "host    all   all   ::1/128       scram-sha-256" >> "$HBA"
    psu "psql -p $PGPORT -h $PGSOCK -d postgres -Atqc 'SELECT pg_reload_conf()'" >/dev/null
    echo "   $HBA: the application's host rule added"
fi

# ---------------------------------------------------------- 3. the adapter --
# `DatabaseManager::Connection()` includes
# `_japp/model/lib/db/adapter/<adapter>.php` and instantiates `DB_<adapter>`,
# so this file's name and its two class names are the framework's, not a
# choice. What is in it is the difference between the two drivers, measured
# against the recorded behaviour of the mysqli member rather than reasoned
# about: see the comments on each override.
install -d -m 755 "$ADAPTERS"
cat > "$ADAPTERS/pdo_pgsql.php" <<'PDO_PGSQL_PHP'
<?php
namespace jf;
/**
 * jFramework PDO_PgSQL driver
 * PostgreSQL 17 member of the DatabaseManager adapter family.
 */
class DB_pdo_pgsql extends BaseDatabase
{
	/**
	 * the actual DB object
	 * @var PDO
	 */
	public $DB;

	/**
	 * Debug mode. if set to true DBAL is intended to generate debug output
	 * @var boolean
	 */
	public $Debug=false;

	protected  $m_databasename;
	function __construct(DatabaseSetting $db)
	{
		parent::__construct($db);
		if ($db->Username and $db->Username != "")
		{
			$this->DB = new \PDO ( "pgsql:dbname={$db->DatabaseName};host={$db->Host}",$db->Username,$db->Password);
			$this->DB->setAttribute(\PDO::ATTR_ERRMODE, \PDO::ERRMODE_SILENT);
		}
		else
			$this->DB = null; //this is mandatory for no-database jFramework
		$this->m_databasename = $db->DatabaseName;
	}

	function __destruct()
	{
		if ($this->DB) $this->DB = null; //destroys the PDO object
	}

	/**
	 * The identity value the last statement generated, or 0.
	 *
	 * MySQLi answers this out of the connection: `insert_id` is whatever the
	 * previous statement generated for an AUTO_INCREMENT column, and a plain
	 * zero when it generated nothing -- which is the difference
	 * BaseDatabase::SQL() leans on when it falls back to rowCount(). PDO's
	 * lastInsertId() cannot say that: with no sequence named it asks for
	 * `lastval()`, which is the last sequence touched anywhere in the session
	 * and so keeps answering with a stale row id long after the statement that
	 * made it. The statement fills this in instead, from its own RETURNING.
	 * @var integer
	 */
	public $InsertID = 0;

	/**
	 * Which column of a table carries its identity, or null for none.
	 * Asked once per table and kept, since every INSERT wants the answer.
	 * @var array
	 */
	public $IdentityColumns = array ();

	function IdentityColumn($Table)
	{
		$key = strtolower ( $Table );
		if (! array_key_exists ( $key, $this->IdentityColumns ))
		{
			$q = $this->DB->prepare ( "SELECT a.attname
					FROM pg_attribute a
					WHERE a.attrelid = to_regclass(?) AND a.attnum > 0
					  AND NOT a.attisdropped AND a.attidentity <> ''" );
			$this->IdentityColumns [$key] = null;
			if ($q and $q->execute ( array ($Table) ))
			{
				$r = $q->fetchColumn ();
				if ($r !== false)
					$this->IdentityColumns [$key] = $r;
			}
		}
		return $this->IdentityColumns [$key];
	}

	function LastID()
	{
		return $this->InsertID;
	}

	function quote($Param)
	{
		$args = func_get_args ();
		if (count($args)>1)
		{
			foreach ( $args as &$arg )
				if ($x = $this->DB->quote ( $arg )) $arg = $x;
		}
		else
			return $this->DB->quote($args[0]);
	}

	function query($QueryString)
	{
		if (! $this->DB) return null;
		$this->QueryCount += 1;
		return $this->DB->query ( $QueryString );
	}

	function exec($Query)
	{
		if (! $this->DB) return null;
		$this->QueryCount += 1;
		return $this->DB->exec($Query);
	}

	function PrepareStatement($Query)
	{
		return new DB_Statement_pdo_pgsql($this, $Query);
	}

	/**
	 * information_schema.table_schema is a schema name in PostgreSQL, not a
	 * database name, so BaseDatabase::ListTables() finds nothing here.
	 */
	public function ListTables($All=false)
	{
		$TablesQuery = $this->SQL ( "SELECT table_name
				FROM information_schema.tables
				WHERE table_schema = current_schema() AND table_type = 'BASE TABLE'" );
		$out = array ();
		if (is_array ( $TablesQuery ))
		{
			foreach ( $TablesQuery as $t )
			{
				$prefix=substr($t ['table_name'], 0, strlen($this->Config->TablePrefix));
				if($All or $prefix==$this->Config->TablePrefix )
					$out [] = $t ['table_name'];
			}
		}
		$this->ListOfTable = true;
		return $out;
	}
}

class DB_Statement_pdo_pgsql extends BaseDatabaseStatement
{
	private $DBAL;
	private $Statement;
	private $_query;
	private $_params;

	/**
	 * The columns this statement returns as `character(n)`, or null until the
	 * first row is asked for.
	 *
	 * MySQL pads a CHAR column out to its width on the way in and strips the
	 * padding again on the way out, so the application has never seen a
	 * trailing space on one. PostgreSQL keeps the padding on the way out, and
	 * `jf_rbac_permissions.Title`, `jf_session.IP` and `jf_stats.IP` are all
	 * CHAR: left alone, every title read back is 64 characters wide and every
	 * address 15. The names are collected once per statement and the padding
	 * taken off each row, which is what the MySQL driver handed over.
	 * @var array
	 */
	private $_blankPadded = null;

	/**
	 * Set when the query is an INSERT that leaves its table's identity column
	 * to the server; the column is then appended as a RETURNING clause so the
	 * generated value comes back with the statement that generated it.
	 * @var string
	 */
	private $_returning = null;

	function __construct(DB_pdo_pgsql $DB,$Query)
	{
		$this->DBAL = $DB;
		$this->_query=$Query;
		if (preg_match ( '/^\s*INSERT\s+INTO\s+([A-Za-z_][A-Za-z_0-9$]*)\s*(\(([^)]*)\))?/i', $Query, $m ))
		{
			$col = $DB->IdentityColumn ( $m [1] );
			if ($col !== null)
			{
				$named = isset ( $m [3] ) ? array_map ( 'trim', explode ( ",", strtolower ( $m [3] ) ) ) : array ();
				if (! in_array ( strtolower ( $col ), $named ))
				{
					$this->_returning = $col;
					$Query .= " RETURNING " . $col;
				}
			}
		}
		$this->Statement=$DB->DB->prepare($Query);
		if ($this->Statement === false)
			$this->Raise ( $DB->DB->errorInfo () );
	}

	/**
	 * The adapter family's contract for a statement the server will not
	 * accept: DB_Statement_mysqli throws out of its constructor, because
	 * mysqli's prepare is what talks to the server and so is where the
	 * complaint arrives. PDO_PgSQL's prepare does not; the statement is
	 * sent when it is executed, so that is where the same complaint
	 * arrives and the same exception is raised.
	 *
	 * Only a statement the server could not make sense of raises, which is
	 * what mysqli's prepare failure meant: SQLSTATE class 42 -- syntax
	 * error, undefined column, undefined table -- and class 0A, which is
	 * what `1; DROP TABLE x` earns. A row that violates a constraint, or a
	 * value the column cannot hold, is an ordinary false here as it was an
	 * ordinary false out of mysqli_stmt::execute.
	 */
	private function Raise($info)
	{
		$state = isset ( $info [0] ) ? substr ( ( string ) $info [0], 0, 2 ) : "";
		if ($state == "42" or $state == "0A")
			throw new \Exception ( "PgSQL error: " . $info [2] );
		return false;
	}

	function __destruct()
	{
		if ($this->Statement)
			$this->Statement = null;
	}

	function bindAll()
	{
		$args = func_get_args ();
		$this->_params=$args;
		$i = 0;
		foreach ( $args as &$arg )
			$this->Statement->bindValue ( ++ $i, $arg );
	}

	function execute()
	{
		if (func_num_args () >= 1)
		{
			$args = func_get_args ();
			call_user_func_array ( array (
				$this, "bindAll"
			), $args );
		}
		$this->DBAL->increaseQueryCount();
		$args=array_merge(array($this->_query),func_get_args());
		$this->DBAL->QueryStart ($args);
		$r=$this->Statement->execute ();
		$this->DBAL->QueryEnd ();
		if ($r === false)
			return $this->Raise ( $this->Statement->errorInfo () );
		// Whatever this statement generated, and nothing that an earlier one
		// did: an INSERT with no identity of its own reports 0, exactly as
		// mysqli_stmt::$insert_id does, so SQL() falls back to rowCount().
		$this->DBAL->InsertID = 0;
		if ($r and $this->_returning !== null)
		{
			$id = $this->Statement->fetchColumn ();
			if ($id !== false and $id !== null)
				$this->DBAL->InsertID = ( int ) $id;
		}
		return $r;
	}

	function rowCount()
	{
		$res=$this->Statement->rowCount ();
		return $res;
	}

	/**
	 * PDO_PgSQL hands a `bytea` column back as an open stream where the MySQL
	 * driver handed back the bytes themselves, and `jf_options.Value` goes
	 * straight into `unserialize()`. The stream is read out here, so a row
	 * carries strings the way it always did.
	 *
	 * An exhausted result is `null`, not `false`: the callers all the way up
	 * -- `UserIDExists()` asks `$res !== null` -- read the empty result as a
	 * null, and BaseDatabaseStatement::fetchAll() stops on either.
	 */
	function fetch()
	{
		$row = $this->Statement->fetch ( \PDO::FETCH_ASSOC );
		if ($row === false or $row === null)
			return null;
		foreach ( $row as $k => $v )
			if (is_resource ( $v ))
				$row [$k] = stream_get_contents ( $v );
		if ($this->_blankPadded === null)
			$this->FindBlankPadded ();
		foreach ( $this->_blankPadded as $k )
			if (isset ( $row [$k] ) and is_string ( $row [$k] ))
				$row [$k] = rtrim ( $row [$k], " " );
		return new PgRow ( $row );
	}

	/**
	 * Which of this statement's output columns are blank-padded.
	 * Only a column the server itself calls `bpchar` is trimmed, so a genuine
	 * trailing space in a varchar or a text column is still delivered.
	 */
	private function FindBlankPadded()
	{
		$this->_blankPadded = array ();
		$n = $this->Statement->columnCount ();
		for($i = 0; $i < $n; ++ $i)
		{
			$m = @$this->Statement->getColumnMeta ( $i );
			if (is_array ( $m ) and isset ( $m ['native_type'] ) and $m ['native_type'] == "bpchar")
				$this->_blankPadded [] = $m ['name'];
		}
	}

	/**
	 * A select that matched nothing is `null`, not an empty array.
	 *
	 * The application tests a result for emptiness in three different ways --
	 * `if ($res)`, `if ($res !== null)`, `if (count($res))` -- and only the
	 * first of them agrees with `array()`. `UserIDExists()` is the plain case:
	 * it returns `$res !== null`, so an empty array makes every user id exist
	 * and no account can ever be created. The rows themselves are unchanged;
	 * only the empty result has a name to keep.
	 */
	function fetchAll()
	{
		$out = array ();
		while ( $r = $this->fetch () )
			$out [] = $r;
		return count ( $out ) == 0 ? null : $out;
	}
}

/**
 * A result row whose keys are matched the way MySQL matched them.
 *
 * PostgreSQL folds an unquoted identifier to lower case, so `SELECT * FROM
 * jf_users` comes back keyed `username` where MySQL keyed it `Username`, and
 * every `$Row["Username"]` in the application would read a missing key. The
 * lookup is the only thing that differs, so the lookup is what is corrected:
 * the row keeps the names the server gave it, and finds them again however
 * the caller spells them.
 */
class PgRow implements \ArrayAccess, \IteratorAggregate, \Countable, \JsonSerializable
{
	private $Data;
	private $Folded;

	function __construct(array $Data)
	{
		$this->Data = $Data;
		$this->Folded = array ();
		foreach ( $Data as $k => $v )
			$this->Folded [strtolower ( ( string ) $k )] = $k;
	}

	private function Key($offset)
	{
		if (array_key_exists ( $offset, $this->Data ))
			return $offset;
		$f = strtolower ( ( string ) $offset );
		return isset ( $this->Folded [$f] ) ? $this->Folded [$f] : null;
	}

	#[\ReturnTypeWillChange]
	function offsetExists($offset)
	{
		$k = $this->Key ( $offset );
		return $k !== null and $this->Data [$k] !== null;
	}

	#[\ReturnTypeWillChange]
	function offsetGet($offset)
	{
		$k = $this->Key ( $offset );
		return $k === null ? null : $this->Data [$k];
	}

	#[\ReturnTypeWillChange]
	function offsetSet($offset, $value)
	{
		if ($offset === null)
		{
			$this->Data [] = $value;
			return;
		}
		$k = $this->Key ( $offset );
		if ($k === null)
		{
			$k = $offset;
			$this->Folded [strtolower ( ( string ) $offset )] = $offset;
		}
		$this->Data [$k] = $value;
	}

	#[\ReturnTypeWillChange]
	function offsetUnset($offset)
	{
		$k = $this->Key ( $offset );
		if ($k !== null)
		{
			unset ( $this->Data [$k] );
			unset ( $this->Folded [strtolower ( ( string ) $k )] );
		}
	}

	#[\ReturnTypeWillChange]
	function getIterator()
	{
		return new \ArrayIterator ( $this->Data );
	}

	#[\ReturnTypeWillChange]
	function count()
	{
		return count ( $this->Data );
	}

	#[\ReturnTypeWillChange]
	function jsonSerialize()
	{
		return $this->Data;
	}

	function ToArray()
	{
		return $this->Data;
	}
}
?>
PDO_PGSQL_PHP
chmod 644 "$ADAPTERS/pdo_pgsql.php"
chown www-data:www-data "$ADAPTERS/pdo_pgsql.php" 2>/dev/null || true
php -l "$ADAPTERS/pdo_pgsql.php"

# ----------------------------------------------------------- 4. the queries --
# Sixty-eight edits over twenty-two files. The script asserts the number of
# occurrences it expects before it makes any replacement, so a tree that is not
# the one it was written against stops the migration rather than quietly
# leaving MySQL behind. Each group of edits carries the reason for it.
: > "$SCRATCH/edited.txt"
python3 - "$APP" <<'PATCH_SQL' | tee -a "$SCRATCH/edited.txt"
# -*- coding: utf-8 -*-
"""Rewrite the application's MySQL SQL as PostgreSQL 17 SQL, in place under /app.

Every edit asserts how many occurrences it expected before it replaces them, so
a source that is not what this script was written against stops the migration
rather than silently leaving MySQL syntax behind.

Two rules run through all of it, and they are the same rule seen from both
ends. A back-quoted *reference* -- `Lft`, `jf_users` -- loses its quoting
entirely, because the migrated schema folded its names and an unquoted
PostgreSQL identifier folds the same way. A back-quoted *alias* -- AS `Left` --
becomes a double-quoted alias, because the PHP that reads the row spells the
key `Left` and a folded alias would come back `left`.
"""
import io, os, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "/app"
EDITS = 0


def patch(rel, pairs):
    global EDITS
    p = os.path.join(ROOT, rel)
    s = io.open(p, encoding="utf-8", newline="").read()
    for old, new, n in pairs:
        got = s.count(old)
        assert got == n, "%s: expected %d of %r, found %d" % (rel, n, old[:70], got)
        s = s.replace(old, new)
        EDITS += n
    io.open(p, "w", encoding="utf-8", newline="").write(s)
    print("  %s" % rel)


# ------------------------------------------------- settings: REPLACE INTO ---
patch("_japp/model/lib/settings.php", [(
    'prepare( "REPLACE INTO {$this->TablePrefix()}options (Name,Value, UserID, Expiration) VALUES (?,?,?,?);")',
    'prepare( "INSERT INTO {$this->TablePrefix()}options (Name,Value, UserID, Expiration) VALUES (?,?,?,?)'
    ' ON CONFLICT (UserID,Name) DO UPDATE SET Value=EXCLUDED.Value, Expiration=EXCLUDED.Expiration")', 1)])

# ----------------------------------------------------- i18n: REPLACE INTO ---
patch("_japp/plugin/i18n.php", [
    ('j::SQL("REPLACE INTO jfp_i18n_graph (ID1,ID2) VALUES (?,?)",$ID1,$ID2);',
     'j::SQL("INSERT INTO jfp_i18n_graph (ID1,ID2) VALUES (?,?)'
     ' ON CONFLICT (ID1,ID2) DO NOTHING",$ID1,$ID2);', 1),
    ('j::SQL("DELETE FROM jfp_i18n_graph WHERE ID1=? AND ID2=? LIMIT 1",$ID1,$ID2);',
     'j::SQL("DELETE FROM jfp_i18n_graph WHERE ID1=? AND ID2=?",$ID1,$ID2);', 1),
])

# ------------------------------------------- xuser: UPDATE ... ID=? LIMIT 1 ---
# The ID these six update is the primary key, so the row cap MySQL wanted was
# never doing anything a unique key was not already doing.
patch("_japp/model/lib/xuser.php", [("WHERE ID=? LIMIT 1", "WHERE ID=?", 6)])

# ---------------------------------------------------- nested set: back-quotes ---
patch("_japp/model/lib/db/nestedset/base.php", [
    ('/2 AS `Count` FROM', '/2 AS \\"Count\\" FROM', 1),
    ('AS `Left`,{$this->Right()} AS `Right`', 'AS \\"Left\\",{$this->Right()} AS \\"Right\\"', 2),
    ('{$this->Right()} - 1, `".\n            $this->Left."` =', '{$this->Right()} - 1, ".\n            $this->Left." =', 1),
    ('- 2 WHERE `".\n            $this->Right."` >', '- 2 WHERE ".\n            $this->Right." >', 1),
    ('- 2 WHERE `".\n            $this->Left."` >', '- 2 WHERE ".\n            $this->Left." >', 1),
    ('SELECT {$this->Right()} AS `Right`"', 'SELECT {$this->Right()} AS \\"Right\\""', 1),
    ('SELECT {$this->Left()} AS `Left`"', 'SELECT {$this->Left()} AS \\"Left\\""', 1),
])

# ------------------------------------------------- nested set: table locks ---
# MySQL's LOCK TABLES is a session-level named lock that is taken and released
# outside any transaction. PostgreSQL's LOCK TABLE is transaction-scoped and is
# an error outside a transaction block, so the session-level equivalent is an
# advisory lock keyed on the table.
patch("_japp/model/lib/db/nestedset/full.php", [
    ('AS `Left`,{$this->Right()} AS `Right`', 'AS \\"Left\\",{$this->Right()} AS \\"Right\\"', 2),
    ('SELECT {$this->Right()} AS `Right`"', 'SELECT {$this->Right()} AS \\"Right\\""', 1),
    ('AS `Right`, {$this->Left()} AS `Left`"', 'AS \\"Right\\", {$this->Left()} AS \\"Left\\""', 1),
    ('$FieldsString.="`".$k."`";', '$FieldsString.=$k;', 2),
    ('$FieldsString.="`".$k."`=?";', '$FieldsString.=$k."=?";', 1),
    ('jf::db()->query("LOCK TABLE {$this->Table()} WRITE");',
     'jf::db()->query("SELECT pg_advisory_lock(hashtext(\'{$this->Table()}\'))");', 1),
    ('jf::db()->query("UNLOCK TABLES");',
     'jf::db()->query("SELECT pg_advisory_unlock(hashtext(\'{$this->Table()}\'))");', 1),
    # `LIMIT 1` over a condition that matches more than one row. InnoDB answers
    # these two out of the index on the column being tested, whose entries for
    # one key are held in primary-key order, so the row that came back was
    # always the one with the lowest ID -- and the RBAC installer builds trees
    # that do have repeated titles, so more than one row is the normal case.
    # PostgreSQL reads the heap and returns whichever row it reaches first,
    # which after a rewrite is not the oldest one. The order the storage engine
    # supplied is now asked for.
    ('{$this->Table()} WHERE $ConditionString LIMIT 1";',
     '{$this->Table()} WHERE $ConditionString ORDER BY {$this->ID()} LIMIT 1";', 1),
    ('\n\t\t\tWHERE $ConditionString LIMIT 1";',
     '\n\t\t\tWHERE $ConditionString ORDER BY {$this->ID()} LIMIT 1";', 1),
])

# ------------------------------------------------------------------- RBAC ---
# GROUP_CONCAT with a SEPARATOR is string_agg; PostgreSQL will not resolve an
# output name in HAVING, so the aggregate is written out there as well; and the
# ORDER BY named a column of the grouped-away side.
#
# `Title` is a CHAR(64). MySQL takes the blank padding off a CHAR on the way
# out, so GROUP_CONCAT joined `root` to `single` and got `root/single`;
# PostgreSQL keeps the padding, and string_agg would glue 64-character columns
# together into a path that matches nothing and leaves every permission
# unfindable. The trim that the MySQL column type performed is stated.
patch("_japp/model/lib/rbac/base.php", [
    ("GROUP_CONCAT(parent.Title ORDER BY parent.Lft SEPARATOR '/' ) AS Path",
     "string_agg(rtrim(parent.Title), '/' ORDER BY parent.Lft) AS Path", 1),
    ("\t\t\t\tHAVING Path = ?\n\t\t\t\tORDER BY parent.Lft\n",
     "\t\t\t\tHAVING string_agg(rtrim(parent.Title), '/' ORDER BY parent.Lft) = ?\n"
     "\t\t\t\tORDER BY node.ID\n", 1),
    # The counter reset. PostgreSQL restarts an identity column; the two
    # assignment tables never had a counter to restart. The MySQL statement
    # does not stay behind as a branch nobody takes: this tree has no MySQL in
    # it any more, and an unreachable `AUTO_INCREMENT` is still MySQL in it.
    ('''		if ($Adapter == "mysqli" or $Adapter == "pdo_mysql")
			jf::SQL ( "ALTER TABLE {$this->TablePrefix()}rbac_{$this->Type()} AUTO_INCREMENT=1 " );
		elseif ($Adapter == "pdo_sqlite")''',
     '''		if ($Adapter == "pdo_pgsql")
			jf::SQL ( "ALTER TABLE {$this->TablePrefix()}rbac_{$this->Type()} ALTER COLUMN ID RESTART WITH 1" );
		elseif ($Adapter == "pdo_sqlite")''', 1),
    ('''		if ($Adapter == "mysqli" or $Adapter == "pdo_mysql")
			jf::SQL ( "ALTER TABLE {$this->TablePrefix()}rbac_rolepermissions AUTO_INCREMENT =1 " );
		elseif ($Adapter == "pdo_sqlite")''',
     '''		if ($Adapter == "pdo_pgsql")
			/* (RoleID,PermissionID) is the whole key; there is no counter */;
		elseif ($Adapter == "pdo_sqlite")''', 1),
])

patch("_japp/model/lib/rbac/permissions.php", [
    ('SELECT RoleID AS `ID` FROM', 'SELECT RoleID AS \\"ID\\" FROM', 1),
    ('SELECT `TP`.* FROM {$this->TablePrefix()}rbac_rolepermissions AS `TR`',
     'SELECT TP.* FROM {$this->TablePrefix()}rbac_rolepermissions AS TR', 1),
    ('RIGHT JOIN {$this->TablePrefix()}rbac_roles AS `TP` ON (`TR`.RoleID=`TP`.ID)',
     'RIGHT JOIN {$this->TablePrefix()}rbac_roles AS TP ON (TR.RoleID=TP.ID)', 1),
])

patch("_japp/model/lib/rbac/roles.php", [
    ('SELECT PermissionID AS `ID` FROM', 'SELECT PermissionID AS \\"ID\\" FROM', 1),
    ('SELECT `TP`.* FROM {$this->TablePrefix()}rbac_rolepermissions AS `TR`',
     'SELECT TP.* FROM {$this->TablePrefix()}rbac_rolepermissions AS TR', 1),
    ('RIGHT JOIN {$this->TablePrefix()}rbac_permissions AS `TP` ON (`TR`.PermissionID=`TP`.ID)',
     'RIGHT JOIN {$this->TablePrefix()}rbac_permissions AS TP ON (TR.PermissionID=TP.ID)', 1),
])

patch("_japp/model/lib/rbac/users.php", [
    ('{$this->TablePrefix()}rbac_userroles AS `TRel`\n\t\t\tJOIN {$this->TablePrefix()}rbac_roles AS `TR` ON\n\t\t\t(`TRel`.RoleID=`TR`.ID)',
     '{$this->TablePrefix()}rbac_userroles AS TRel\n\t\t\tJOIN {$this->TablePrefix()}rbac_roles AS TR ON\n\t\t\t(TRel.RoleID=TR.ID)', 1),
    ('''		if ($Adapter == "mysqli" or $Adapter == "pdo_mysql")
			jf::SQL ( "ALTER TABLE {$this->TablePrefix()}rbac_userroles AUTO_INCREMENT =1 " );
		elseif ($Adapter == "pdo_sqlite")''',
     '''		if ($Adapter == "pdo_pgsql")
			/* (UserID,RoleID) is the whole key; there is no counter */;
		elseif ($Adapter == "pdo_sqlite")''', 1),
])


# ----------------------------------------------------- the stats plugin DDL ---
# Runs on every request. PostgreSQL has no inline secondary indexes and no
# prefix indexes, so the KEY clauses become CREATE INDEX statements over
# left(col, n) -- which is the form the migrated schema file already uses.
patch("app/plugin/stats.php", [('''		jf::SQL("CREATE TABLE IF NOT EXISTS `{$this->TablePrefix()}stats` (
		  `ID` int(11) NOT NULL AUTO_INCREMENT,
		  `UserID` int(11) NOT NULL,
		  `SessionID` varchar(128) COLLATE utf8_bin NOT NULL,
		  `Timestamp` int(11) NOT NULL,
		  `Page` varchar(1024) COLLATE utf8_bin NOT NULL,
		  `Query` varchar(1024) COLLATE utf8_bin NOT NULL,
		  `IP` char(15) COLLATE utf8_bin NOT NULL,
		  `Host` varchar(256) COLLATE utf8_bin NOT NULL,
		  `Protocol` varchar(10) COLLATE utf8_bin NOT NULL,
		  `UserAgent` varchar(1024) COLLATE utf8_bin NOT NULL,
		  PRIMARY KEY (`ID`),
		  KEY `UserID` (`UserID`),
		  KEY `SessionID` (`SessionID`),
		  KEY `Timestamp` (`Timestamp`),
		  KEY `Page` (`Page`(255)),
		  KEY `IP` (`IP`),
		  KEY `Host` (`Host`(255)),
		  KEY `Protocol` (`Protocol`)
		) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_bin AUTO_INCREMENT=1");''',
 '''		$T = $this->TablePrefix() . "stats";
		jf::SQL("CREATE TABLE IF NOT EXISTS {$T} (
		  ID integer GENERATED BY DEFAULT AS IDENTITY NOT NULL,
		  UserID integer NOT NULL,
		  SessionID character varying(128) COLLATE \\"C\\" NOT NULL,
		  Timestamp integer NOT NULL,
		  Page character varying(1024) COLLATE \\"C\\" NOT NULL,
		  Query character varying(1024) COLLATE \\"C\\" NOT NULL,
		  IP character(15) COLLATE \\"C\\" NOT NULL,
		  Host character varying(256) COLLATE \\"C\\" NOT NULL,
		  Protocol character varying(10) COLLATE \\"C\\" NOT NULL,
		  UserAgent character varying(1024) COLLATE \\"C\\" NOT NULL,
		  PRIMARY KEY (ID)
		)");
		foreach (array(
			"userid" => "UserID",
			"sessionid" => "SessionID",
			"timestamp" => "Timestamp",
			"page" => "left(Page, 255)",
			"ip" => "IP",
			"host" => "left(Host, 255)",
			"protocol" => "Protocol",
		) as $Name => $Expression)
			jf::SQL("CREATE INDEX IF NOT EXISTS {$T}_{$Name}_idx ON {$T} ({$Expression})");''', 1)])

# ------------------------------------------------------- the panel dashboard ---
patch("_japp/view/default/panel/dashboard.php", [
    ('SELECT S.*,U.* FROM `".reg("jf/session/table/name")."` AS S LEFT JOIN `".jf_Users_Table_Name."` AS U ON (\n        \tS.`".reg("jf/session/table/UserID")."`=U.`".jf_Users_Table_UserID."`)',
     'SELECT S.*,U.* FROM ".reg("jf/session/table/name")." AS S LEFT JOIN ".jf_Users_Table_Name." AS U ON (\n        \tS.".reg("jf/session/table/UserID")."=U.".jf_Users_Table_UserID.")', 1),
    ('SELECT * FROM `".reg("jf/log/table/name")."` WHERE `".reg("jf/log/table/Severity")."`>=5 ORDER BY `".reg("jf/log/table/Timestamp")."` DESC LIMIT 50',
     'SELECT * FROM ".reg("jf/log/table/name")." WHERE ".reg("jf/log/table/Severity").">=5 ORDER BY ".reg("jf/log/table/Timestamp")." DESC LIMIT 50', 1),
])

# ---------------------------------------------------------- the lesson resets ---
patch("challenges/single/XSS2/index.php", [
    ('"(id int auto_increment primary key, message text)"',
     '"(id int GENERATED BY DEFAULT AS IDENTITY primary key, message text)"', 1)])
patch("challenges/single/NumericSQLInjection/index.php", [
    ('"(station int auto_increment primary key,',
     '"(station int GENERATED BY DEFAULT AS IDENTITY primary key,', 1)])
patch("challenges/single/BusinessLayerAccessControl/index.php", [
    ('"(id int not null auto_increment primary key,',
     '"(id int GENERATED BY DEFAULT AS IDENTITY primary key,', 1)])

# ----------------------------------------------- the row that leaves as JSON ---
# `/mode/workshop/user/get` hands this result straight to json_encode(), so the
# column names are part of the response body. Unquoted, PostgreSQL folds them
# and the endpoint starts answering `{"id":..,"username":..}`; quoted aliases
# keep the names the endpoint has always published.
patch("app/model/workshop/users.php", [(
    '"SELECT Username, ID from {$this->TablePrefix()}users inner join',
    '"SELECT Username AS \\"Username\\", ID AS \\"ID\\" from {$this->TablePrefix()}users inner join', 1)])

# ------------------------------------------------- the unaliased aggregate ---
# MySQL names the output column of a bare aggregate after the expression that
# produced it, so `SELECT COUNT(*)` comes back keyed `COUNT(*)`; PostgreSQL
# names it after the function, `count`. Neither name is worth depending on:
# the two readers get an alias to read.
patch("_japp/model/lib/user.php", [
    ('jf::SQL("SELECT COUNT(*) FROM {$this->TablePrefix()}users");\n\t\treturn $res[0][\'COUNT(*)\'];',
     'jf::SQL("SELECT COUNT(*) AS UserCount FROM {$this->TablePrefix()}users");\n'
     '\t\treturn $res[0][\'UserCount\'];', 1)])
patch("_japp/model/lib/rbac/base.php", [
    ('jf::SQL ( "SELECT COUNT(*) FROM {$this->TablePrefix()}rbac_{$this->Type()}" );\n\t\treturn $Res [0] [\'COUNT(*)\'];',
     'jf::SQL ( "SELECT COUNT(*) AS EntityCount FROM {$this->TablePrefix()}rbac_{$this->Type()}" );\n'
     '\t\treturn $Res [0] [\'EntityCount\'];', 1)])

# ------------------------------------------------- case-insensitive lookup ---
# `LOWER()` is not the same function on the two servers. MySQL folds with the
# case table of the column's *character set*, so a `utf8` column folded the same
# way whatever its collation was -- including `utf8_bin`, which is how every
# name and address in this schema is stored. PostgreSQL folds with the
# *collation*, and the migrated columns carry `COLLATE "C"` to keep utf8_bin's
# byte ordering and byte equality; C folds ASCII and nothing else. Left alone,
# `LOWER(Username)=LOWER(?)` stops matching `Ä` against `ä` and stops matching
# the Turkish dotted capital `\u0130` against `i`, so a name that the old site
# considered taken can be registered a second time.
#
# The fold is asked for by name instead. `pg_c_utf8` is PostgreSQL 17's builtin
# C.UTF-8: full Unicode simple case mapping, deterministic -- so the `=` around
# it is still byte equality, exactly as utf8_bin was -- and, being builtin, the
# same on every installation rather than whatever libc was compiled in.
_FOLD = ('LOWER(%s)=LOWER(?)',
         'LOWER(%s COLLATE \\"pg_c_utf8\\")=LOWER(? COLLATE \\"pg_c_utf8\\")')
patch("_japp/model/lib/user.php", [
    (_FOLD [0] % "Username", _FOLD [1] % "Username", 6)])
patch("_japp/model/lib/xuser.php", [
    (_FOLD [0] % "Email", _FOLD [1] % "Email", 1)])

# ------------------------------------------------------- unordered results ---
# A SELECT with no ORDER BY. InnoDB answers a scan out of the clustered index,
# so these came back in primary-key order every time, and the application reads
# them as though that were promised: `getActiveID()` takes row 0 of its result,
# and each of the listings renders its rows in the order they arrive.
# PostgreSQL reads the heap, where the order is wherever the last UPDATE
# happened to put the row -- so the contest challenge table, which the corpus
# updates, comes back in a different order on each run. The order the storage
# engine used to supply is written down.
patch("app/model/workshop/users.php", [(
    'on ID = UserID where RoleID = ?"', 'on ID = UserID where RoleID = ? ORDER BY ID"', 1)])
patch("app/model/contest/users.php", [(
    'on ID = UserID where RoleID = ?"', 'on ID = UserID where RoleID = ? ORDER BY ID"', 1)])
patch("app/model/contest/challenges.php", [(
    '"SELECT * FROM ".self::TABLE_NAME." WHERE ContestID = ?"',
    '"SELECT * FROM ".self::TABLE_NAME." WHERE ContestID = ? ORDER BY ID"', 1)])
patch("app/model/contest/details.php", [(
    '"SELECT * FROM ".self::TABLE_NAME." WHERE EndTimestamp > $currentTime"',
    '"SELECT * FROM ".self::TABLE_NAME." WHERE EndTimestamp > $currentTime ORDER BY ID"', 3)])
patch("challenges/single/XSS2/index.php", [(
    '\\jf::SQL("SELECT * FROM ".self::TABLE_NAME);',
    '\\jf::SQL("SELECT * FROM ".self::TABLE_NAME." ORDER BY id");', 1)])
patch("challenges/single/BusinessLayerAccessControl/index.php", [(
    '\\jf::SQL("SELECT * FROM ".self::TABLE_NAME);',
    '\\jf::SQL("SELECT * FROM ".self::TABLE_NAME." ORDER BY id");', 1)])
patch("challenges/single/NumericSQLInjection/index.php", [(
    '\\jf::SQL("SELECT * FROM ".self::TABLE_NAME);',
    '\\jf::SQL("SELECT * FROM ".self::TABLE_NAME." ORDER BY station");', 1)])

# The roles a user holds. The join drives off `jf_rbac_userroles`, whose primary
# key is (UserID, RoleID), so InnoDB walked the rows of one user in RoleID order
# and every caller that reads row 0 -- the lesson banner prints
# `AllRoles($u)[0]["Title"]` -- got the lowest-numbered role. PostgreSQL has no
# such habit and hands back whichever side of the hash join it finished with.
patch("_japp/model/lib/rbac/users.php", [(
    '\t\t\tWHERE TRel.UserID=?", $UserID );',
    '\t\t\tWHERE TRel.UserID=?\n\t\t\tORDER BY TR.ID", $UserID );', 1)])

# ------------------------------------------------ trailing-space comparison ---
# Every MySQL 5.7 collation is PAD SPACE: the comparison ignores trailing
# spaces, so `'david '` equalled `'david'` and `'Larry Stooge  '` equalled
# `'Larry Stooge'`. PostgreSQL's `varchar` is NO PAD and the two are simply
# different strings, so the same login is refused and the same lookup finds
# nobody. It is not a property a PostgreSQL collation can carry -- there is no
# PAD SPACE provider -- so each comparison that the padding reached says what it
# means: the trailing blanks come off both sides, which is what the old server
# did before comparing.
patch("challenges/single/AccessControlMatrix/index.php", [(
    '\jf::SQL("SELECT * FROM ".self::TABLE_NAME." where name = ?", $name);',
    '\jf::SQL("SELECT * FROM ".self::TABLE_NAME." where rtrim(name) = rtrim(?)", $name);', 1)])
patch("challenges/single/BusinessLayerAccessControl/index.php", [(
    '" WHERE id = ? and password = ?", $user, $pass);',
    '" WHERE id = ? and rtrim(password) = rtrim(?)", $user, $pass);', 1)])

print("edits: %d" % EDITS)
PATCH_SQL

# ----------------------------------------------------- 5. the configuration --
# One word, and one removal. The word is the adapter name; `DatabaseSetting`'s
# first argument is what `DatabaseManager` loads.
#
# The removal is `DoctrinePlugin::Load()`'s request-time `updateSchema()`. In
# develop mode the ORM synchronised the database to the entity metadata on
# every request -- survivable against a MySQL schema the metadata had itself
# created, and not against a migrated PostgreSQL schema that
# `install/_db/pdo_pgsql.schema.sql` owns: the metadata knows nothing about
# identity columns, the expression indexes or the declared collations, so each
# request would set about undoing step 02. The metadata scan itself stays,
# because loading it is what pulls in every class under `app/model`.
python3 - "$APP" <<'PATCH_CONFIG' | tee -a "$SCRATCH/edited.txt"
# -*- coding: utf-8 -*-
import io, os, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "/app"


def patch(rel, pairs):
    p = os.path.join(ROOT, rel)
    s = io.open(p, encoding="utf-8", newline="").read()
    for old, new, n in pairs:
        got = s.count(old)
        assert got == n, "%s: expected %d of %r, found %d" % (rel, n, old[:70], got)
        s = s.replace(old, new)
    io.open(p, "w", encoding="utf-8", newline="").write(s)
    print("  %s" % rel)


patch("app/config/application.php", [(
    'new \\jf\\DatabaseSetting("mysqli", "webgoatphp", "webgoat", "webgoat")',
    'new \\jf\\DatabaseSetting("pdo_pgsql", "webgoatphp", "webgoat", "webgoat")', 1)])

patch("app/plugin/doctrine.php", [(
    "\t\tif (jf::$RunMode->IsDevelop())\n\t\t\t$r = self::UpdateSchema();\n",
    "\t\t// The ORM no longer synchronises the schema at request time: the\n"
    "\t\t// migrated schema is owned by install/_db, not by the entity\n"
    "\t\t// metadata. The metadata scan itself stays, because it is what\n"
    "\t\t// loads every class under app/model -- the controllers reach\n"
    "\t\t// webgoat\\LessonScanner and its siblings through no other path.\n"
    "\t\tif (jf::$RunMode->IsDevelop())\n"
    "\t\t\tself::$entityManager->getMetadataFactory ()->getAllMetadata ();\n", 1)])
PATCH_CONFIG

# ------------------------------------------- 6. the source that is not ours --
# Four files whose SQL no PostgreSQL server would accept, and which nothing in
# this deployment loads any more: the mysqli adapter the configuration used to
# name, and Doctrine DBAL's schema manager and connection code for Oracle,
# MySQL and SQL Server. `DoctrinePlugin::Config()` derives its driver from the
# same `DatabaseSetting`, so the DBAL now asks for `pdo_pgsql` and reaches none
# of them.
for f in "$ADAPTERS/mysqli.php" \
         "$APP/app/plugin/doctrine/Doctrine/DBAL/Schema/OracleSchemaManager.php" \
         "$APP/app/plugin/doctrine/Doctrine/DBAL/Driver/PDOMySql/Driver.php" \
         "$APP/app/plugin/doctrine/Doctrine/DBAL/Driver/SQLSrv/SQLSrvConnection.php"; do
    if [ -f "$f" ]; then
        rm "$f"
        echo "  removed ${f#$APP/}"
    fi
done

# ------------------------------------------------------------ 7. it parses --
# Every file the two patch scripts named, through the interpreter that will
# run it. They print what they edited; that list is what is checked, rather
# than a timestamp sweep over 900 files that a restored artifact's mtimes
# would make meaningless.
BAD=0
N=0
while read -r rel; do
    [ -n "$rel" ] || continue
    N=$((N + 1))
    php -l "$APP/$rel" >/dev/null || { echo "syntax error in $rel" >&2; BAD=1; }
done < <(sed -n 's/^  \(.*\.php\)$/\1/p' "$SCRATCH/edited.txt" | sort -u)
[ "$BAD" = 0 ] || exit 1
[ "$N" -ge 20 ] || { echo "only $N files were edited; expected at least 20" >&2; exit 1; }
php -l "$ADAPTERS/pdo_pgsql.php" >/dev/null
echo "   $N edited files parse, and so does the adapter"

chown -R www-data:www-data "$APP"
: > /var/log/apache2/error.log 2>/dev/null || true
apache2ctl -k graceful 2>/dev/null || apache2ctl -k start 2>/dev/null || true
sleep 2

# --------------------------------------------- 8. it serves without MySQL --
# The reading that says the move happened rather than that nothing broke: with
# mysqld stopped, the application still answers, and the answer is one it had
# to reach a database for. `jf_stats` is written by the stats plugin on every
# page, so a row count that moves is a write that landed in PostgreSQL.
BEFORE=$(psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc 'SELECT count(*) FROM jf_stats'")
/etc/init.d/mysql stop >/dev/null 2>&1 || pkill -TERM -x mysqld 2>/dev/null || true
for i in $(seq 1 60); do
    mysql --socket="$MYSOCK" -uroot -e 'SELECT 1' >/dev/null 2>&1 || break
    sleep 1
done
if mysql --socket="$MYSOCK" -uroot -e 'SELECT 1' >/dev/null 2>&1; then
    echo "mysqld is still up; the smoke check would prove nothing" >&2
    exit 1
fi

CODE=$(curl -s -o "$SCRATCH/home.html" -w '%{http_code}' \
       -H 'Host: localhost' http://127.0.0.1/ || true)
if [ "$CODE" != 200 ]; then
    echo "GET / answered $CODE with MySQL stopped" >&2
    tail -40 /var/log/apache2/error.log >&2 || true
    exit 1
fi
grep -qi 'webgoat' "$SCRATCH/home.html" \
    || { echo "GET / did not return the application's page" >&2; exit 1; }

AFTER=$(psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc 'SELECT count(*) FROM jf_stats'")
[ "$AFTER" -gt "$BEFORE" ] || {
    echo "jf_stats did not grow ($BEFORE -> $AFTER): the page was served" \
         "without writing to PostgreSQL" >&2; exit 1; }
echo "   the application answers out of PostgreSQL with mysqld stopped"
echo "     (jf_stats $BEFORE -> $AFTER)"

# And it did so without failing. Fatals, parse errors and uncaught exceptions
# only: the vendored Doctrine 2.3.3 has emitted a `"continue" targeting switch`
# warning on every request since the image was built with PHP 7.4, and a check
# that read warnings would be reporting that rather than this step's work.
if grep -Eq 'PHP (Fatal|Parse) error|Uncaught' /var/log/apache2/error.log 2>/dev/null
then
    echo "PHP failed while serving:" >&2
    grep -E 'PHP (Fatal|Parse) error|Uncaught' /var/log/apache2/error.log \
        | tail -20 >&2
    exit 1
fi

# --------------------------------------------------- 9. put the rows back --
# The smoke check wrote to the database, and the database is a deliverable: it
# has to be handed on holding the rows step 03 migrated, not those rows plus a
# page view. `install/_db/pdo_pgsql.data.sql` is the file that says what they
# are -- it empties each table before filling it, so applying it is a restore
# and not an append.
/usr/local/sbin/wg-services start
for i in $(seq 1 60); do
    mysql --socket="$MYSOCK" -uroot -e 'SELECT 1' >/dev/null 2>&1 && break
    sleep 1
done
[ -f "$DATA" ] || { echo "step 03's $DATA is missing" >&2; exit 1; }
sed "s/PREFIX_/$PREFIX/g" "$DATA" > "$SCRATCH/data.restore.sql"
chmod 644 "$SCRATCH/data.restore.sql"
psql_ "-q -f '$SCRATCH/data.restore.sql'"
RESTORED=$(psu "psql -p $PGPORT -h $PGSOCK -d $DB -Atqc 'SELECT count(*) FROM jf_stats'")
[ "$RESTORED" = "$BEFORE" ] || {
    echo "the restore left jf_stats at $RESTORED, not $BEFORE" >&2; exit 1; }
echo "   the rows are back where step 03 left them"

# The three readings the previous steps end on, because all three are still
# scored here: strict mode, the schema file, the data file.
grep -q 'STRICT_TRANS_TABLES' /etc/mysql/my.cnf \
    || { echo "step 1's sql_mode is gone from /etc/mysql/my.cnf" >&2; exit 1; }
[ -f "$APP/install/_db/pdo_pgsql.schema.sql" ] \
    || { echo "step 2's schema file is gone" >&2; exit 1; }

/usr/local/sbin/wg-services status || true
echo "== step 04 done"
