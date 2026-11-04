<?php
/**
 * The clean control for driverscan.php: a PostgreSQL-wired tree that talks
 * about MySQL constantly and must not produce a single site.
 *
 * The point of this file is the false-positive side. A scanner that reports
 * every occurrence of the string "mysql" would flag nine things here -- a
 * class name, a method name, a comment, a doc block, a migration note, a
 * table name, a variable, an array key, and a thrown message -- and every one
 * of them is a description of the driver that was *removed*. Removing the
 * word from the codebase is not the requirement; removing the driver is.
 *
 * Nothing here is ever loaded. It is parsed.
 */

namespace WgSelfTest;

/** Replaces the old DB_mysqli adapter. The name is prose, not wiring. */
class CleanPostgresAdapter
{
    /**
     * Where the old DSN used to be. The string itself is deliberately *not*
     * kept: a literal shaped like `mysql:host=...;dbname=...` is wiring no
     * matter what the property around it is called, and driverscan is right
     * to report one. Only the description of it survives.
     *
     * @var string
     */
    public $mysql_legacy_note = 'replaced during the pdo_mysql -> pdo_pgsql cut';

    public function connect($host, $user, $pass, $db)
    {
        // Was: new mysqli($host, $user, $pass, $db) -- see notes for why.
        return new \PDO("pgsql:host={$host};dbname={$db}", $user, $pass);
    }

    public function connectProcedural($host, $user, $pass, $db)
    {
        return pg_connect("host=$host user=$user password=$pass dbname=$db");
    }

    public function escape($conn, $s)
    {
        return pg_escape_literal($conn, $s);
    }

    /** Named after the function it replaced; it calls no such function. */
    public function mysqli_real_escape_string_replacement($conn, $s)
    {
        return $this->escape($conn, $s);
    }

    public function mysqli_helper($conn)
    {
        return pg_last_error($conn);
    }

    public function settings($db, $user, $pass)
    {
        return new \jf\DatabaseSetting("pgsql", $db, $user, $pass);
    }

    public function doctrineConfig($db)
    {
        // 'pdo_mysql' was the value here before the switch.
        return array('driver' => 'pdo_pgsql', 'dbname' => $db);
    }

    public function adapterName()
    {
        $adapter = "pdo_pgsql";
        return $adapter;
    }

    public function requireExtension()
    {
        if (!extension_loaded('pgsql')) {
            throw new \RuntimeException(
                'pgsql missing; the mysqli extension is no longer used'
            );
        }
        return true;
    }

    public function auditTable()
    {
        // A table whose name records where the rows came from.
        return 'mysql_import_audit';
    }
}
