<?php
/**
 * One watched literal per context, each in a function whose name begins with
 * the context the scanner must report, plus one watched inheritance edge and
 * one declaration. The expectation is the fixture's own text: adding a case
 * extends the check, and a case that stops being detected fails it by name.
 */

namespace jf\selftest;

use Doctrine\DBAL\Driver\PDOMySql\Driver as MySqlDriver;

class DB_pdo_mysql {}

class arraykey_holder
{
    public function arraykey_driver_table()
    {
        return array("pdo_mysql" => "kept", "pdo_pgsql" => "kept");
    }

    public function arrayvalue_driver_list()
    {
        return array("driver" => "mysqli");
    }

    public function arg_import_path()
    {
        return \jf\jf::import("jf/model/lib/db/adapter/mariadb");
    }

    public function return_driver_name()
    {
        return "mysql";
    }

    public function other_assignment()
    {
        $adapter = "drizzle_pdo_mysql";
        return $adapter;
    }
}

class Subclass extends DB_pdo_mysql {}

class AliasedSubclass extends MySqlDriver {}
