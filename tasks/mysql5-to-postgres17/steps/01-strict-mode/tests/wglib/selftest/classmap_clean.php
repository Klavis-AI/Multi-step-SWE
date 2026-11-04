<?php
/**
 * A PostgreSQL-wired file that talks about MySQL constantly and must produce
 * no watched literal at all. Retiring MySQL means removing the driver, not
 * removing the word: the twelve Doctrine files this deployment keeps mention
 * MySQL only in sentences like these, and a scanner that reported them would
 * demand edits to files the step requires untouched.
 *
 * Migrated from mysqli. The old pdo_mysql adapter lived at
 * _japp/model/lib/db/adapter/pdo_mysql.php and spoke mysql:dbname=webgoatphp.
 */

namespace jf\selftest;

use jf\DB_pdo_pgsql as Base;

// mysql, mysqli, pdo_mysql, mariadb -- in a comment, so not literals.
interface MysqlFreeMarker {}

/** Named for what it replaced; the name is an identifier, not a string. */
class MysqliReplacement extends Base implements MysqlFreeMarker
{
    /** @var string The DSN scheme, once "mysql", now this. */
    public $scheme = "pgsql";

    public function drivers()
    {
        # Doctrine's key for the driver we no longer register was
        # 'pdo_mysql'; ours is the one below.
        return array("pdo_pgsql" => "Doctrine\\DBAL\\Driver\\PDOPgSql\\Driver");
    }

    public function describe()
    {
        return "no longer mysql"; // a sentence, not a driver name
    }
}
