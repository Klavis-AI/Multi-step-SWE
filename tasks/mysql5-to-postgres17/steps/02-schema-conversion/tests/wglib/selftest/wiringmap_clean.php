<?php
/**
 * A PostgreSQL-wired file that says "mysql" everywhere text can say it, and
 * whose reported *shape* must contain the word nowhere.
 *
 * `classmap_clean.php` makes the same point about literals; this makes it
 * about structure, because the four files step 05 edits sit beside twelve
 * Doctrine files that discuss MySQL in docblocks and must be left alone. The
 * switch below is written out a second time inside a nowdoc with its MySQL
 * arms intact, the driver map is written a third time in a docblock, and
 * neither is a node.
 *
 * The positive control is the other half: the three PostgreSQL shapes here are
 * reported, with exactly the values written below. A scanner that reported
 * nothing at all would pass an absence test and prove nothing.
 *
 * Migrated from mysqli; the pdo_mysql arm and the mariaDB arm are gone.
 *
 * The map this class used to declare:
 *   array('pdo_mysql' => 'Doctrine\DBAL\Driver\PDOMySql\Driver',
 *         'mysqli'    => 'Doctrine\DBAL\Driver\Mysqli\Driver')
 */

namespace jf\selftest;

class MysqlFreeHarness
{
    /** @var array Was keyed by mysqli and pdo_mysql. */
    public static $driverMap = array(
        'pdo_pgsql' => 'Doctrine\\DBAL\\Driver\\PDOPgSql\\Driver',
    );

    // mysql, mysqli, pdo_mysql, mariadb -- a comment, not a case label.
    public function build($suite, $userConfig)
    {
        $adapter = $userConfig->Adapter;
        if (!$adapter) {
            $adapter = "pdo_pgsql";     // was "pdo_mysql" until step 05
        }
        switch ($adapter) {
            case "pdo_pgsql":
                $suite->add("jf/test/lib/db/adapter/pdo_pgsql");
                break;
        }
        return $adapter;
    }

    /** The shape this file used to have, as text. */
    public function history()
    {
        return <<<'OLD'
switch ($userConfig->Adapter)
{
    case "mysqli":
    case "pdo_mysql":
    case "mariaDB":
        $this->add("jf/test/lib/db/adapter/pdo_mysql");
        $this->add("jf/test/lib/db/adapter/mysqli");
        break;
}
$adapter = "pdo_mysql";
$map = array("pdo_mysql" => "Doctrine\DBAL\Driver\PDOMySql\Driver");
OLD;
    }

    /** A switch that selects on nothing this application connects with. */
    public function route($method)
    {
        switch ($method) {
            case "GET":
                return "read";
            case "POST":
                return "write";
        }
        return "mysql is only a word here";
    }

    /** An array whose keys are not back-end names. */
    public function connection()
    {
        $conn = array('driver' => 'pdo_pgsql', 'dbname' => 'webgoatphp');
        return $conn;
    }
}
