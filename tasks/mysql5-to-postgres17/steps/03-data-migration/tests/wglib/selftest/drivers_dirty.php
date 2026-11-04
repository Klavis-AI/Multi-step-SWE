<?php
/**
 * The failure control for driverscan.php: one MySQL driver site per method,
 * and nothing else.
 *
 * Each method name begins with the `kind` driverscan must report for it, so
 * the expectation is derived from this file rather than written beside it --
 * adding a case here extends the check automatically, and a case that stops
 * being detected fails the selftest by name. Two classes at the bottom carry
 * the `classdef` kind, which is a declaration and cannot live in a method.
 *
 * Nothing here is ever loaded. It is parsed.
 */

namespace WgSelfTest;

class DirtyDrivers
{
    public function new_mysqli_object($host, $user, $pass, $db)
    {
        return new \mysqli($host, $user, $pass, $db);
    }

    public function new_adapter_class($setting)
    {
        return new \DB_pdo_mysql($setting);
    }

    public function call_procedural_connect($host, $user, $pass)
    {
        return mysqli_connect($host, $user, $pass);
    }

    public function call_legacy_escape($conn, $s)
    {
        return mysql_real_escape_string($s, $conn);
    }

    public function dsn_plain_literal($user, $pass)
    {
        return new \PDO("mysql:host=localhost;dbname=webgoatphp", $user, $pass);
    }

    public function dsn_interpolated($db, $host, $user, $pass)
    {
        return new \PDO("mysql:dbname={$db};host={$host};", $user, $pass);
    }

    public function dsn_concatenated($db, $user, $pass)
    {
        return new \PDO("mysql:dbname=" . $db . ";host=localhost", $user, $pass);
    }

    public function name_array_entry($db)
    {
        return array('driver' => 'pdo_mysql', 'dbname' => $db);
    }

    public function name_config_constructor($db, $user, $pass)
    {
        return new \jf\DatabaseSetting("mysqli", $db, $user, $pass);
    }

    public function name_bare_assignment()
    {
        $adapter = "pdo_mysql";
        return $adapter;
    }

    public function extension_probe()
    {
        return extension_loaded('mysqli');
    }
}

/** classdef: the adapter classes themselves. */
class DB_mysqli
{
    public $DB;
}

class DB_pdo_mysql
{
    public $DB;
}
