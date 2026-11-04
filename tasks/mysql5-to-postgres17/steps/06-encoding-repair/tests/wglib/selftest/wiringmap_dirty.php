<?php
/**
 * One of every shape `wiringmap.php` reports, with the answer written in the
 * fixture rather than in the scorer.
 *
 * `retire.selftest` reads the expectation out of `_WIRINGMAP_*` below and
 * those tuples restate what is here: two arms of one switch including the
 * label-only arm a line filter leaves behind, a guarded assignment, an
 * unguarded one, an inline `?:` fallback, a keyed registry, a plain list, and a
 * registry with one computed value that must come back as null rather than
 * shorten the array.
 */

namespace jf\selftest;

class WiringMapDirty
{
    public static $driverMap = array(
        'pdo_mysql' => 'Doctrine\\DBAL\\Driver\\PDOMySql\\Driver',
        'pdo_pgsql' => 'Doctrine\\DBAL\\Driver\\PDOPgSql\\Driver',
    );

    public function build($userConfig, $klass)
    {
        $plain = "pdo_pgsql";
        $adapter = $userConfig->Adapter;
        if (!$adapter) {
            $adapter = "pdo_mysql";
        }
        $fallback = $userConfig->Legacy ?: "mysqli";

        $keywordLists = array('mysql', 'pgsql');
        $partialMap = array('pdo_mysql' => $klass, 'pdo_pgsql' => 'kept');

        switch ($adapter) {
            case "mysqli":
            case "pdo_mysql":
                $this->add("jf/test/lib/db/adapter/pdo_mysql");
                break;
            case "pdo_sqlite":
        }
        return array($plain, $fallback, $keywordLists, $partialMap);
    }

    public function add($module)
    {
        return $module;
    }
}
