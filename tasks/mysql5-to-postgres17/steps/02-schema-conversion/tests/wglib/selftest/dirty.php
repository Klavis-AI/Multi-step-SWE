<?php
/**
 * The failure control for the dialect gate. Every statement here is MySQL and
 * every one must be reported -- one construct per method, so a gate that has
 * gone blind in one place cannot hide behind the rest.
 *
 * Two classes, because two different layers have to catch them. `Syntax` is
 * what PostgreSQL's own grammar rejects. `Catalog` is what the grammar
 * *accepts* -- `GROUP_CONCAT(x)` is a function call and `COLLATE utf8_bin` is
 * a collation name, and syntax has no opinion about whether either exists --
 * so those are caught by looking the names up in `pg_proc` and `pg_collation`.
 * A gate with only the first layer passes every statement in `Catalog`.
 */

namespace selftest;

class Syntax
{
    public function backticks($id)
    {
        jf::SQL("SELECT `ID`, `Username` FROM `users` WHERE `ID`=?", $id);
    }

    public function updateLimit($id)
    {
        jf::SQL("UPDATE users SET Activated=1 WHERE ID=? LIMIT 1", $id);
    }

    public function deleteLimit($id)
    {
        jf::SQL("DELETE FROM graph WHERE ID1=? AND ID2=? LIMIT 1", $id, $id);
    }

    public function alterAutoIncrement()
    {
        jf::SQL("ALTER TABLE roles AUTO_INCREMENT =1");
    }

    public function replaceInto($name, $value)
    {
        jf::SQL("REPLACE INTO options (Name,Value) VALUES (?,?)", $name, $value);
    }

    public function insertIgnore($name)
    {
        jf::SQL("INSERT IGNORE INTO options (Name) VALUES (?)", $name);
    }

    public function onDuplicateKey($name, $value)
    {
        jf::SQL("INSERT INTO options (Name,Value) VALUES (?,?)"
                . " ON DUPLICATE KEY UPDATE Value=VALUES(Value)", $name, $value);
    }

    public function groupConcatSeparator()
    {
        jf::SQL("SELECT GROUP_CONCAT(Title ORDER BY Lft SEPARATOR '/') AS Path"
                . " FROM roles");
    }

    public function unlockTables()
    {
        jf::SQL("UNLOCK TABLES");
    }

    public function lockTables($table)
    {
        jf::SQL("LOCK TABLES roles WRITE, users READ");
    }

    public function createTableEngine()
    {
        jf::SQL("CREATE TABLE IF NOT EXISTS `stats` (
                   `ID` int(11) NOT NULL AUTO_INCREMENT,
                   `Page` varchar(255) COLLATE utf8_bin NOT NULL,
                   PRIMARY KEY (`ID`),
                   KEY `Page` (`Page`(255))
                 ) ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_bin");
    }

    public function createTableAutoIncrement($table)
    {
        jf::SQL("CREATE TABLE {$table}(id int not null auto_increment"
                . " primary key, message text)");
    }

    public function limitOffsetComma()
    {
        jf::SQL("SELECT * FROM stats ORDER BY ID DESC LIMIT 40, 20");
    }

    public function setNames()
    {
        jf::SQL("SET NAMES utf8 COLLATE utf8_bin");
    }

    public function intervalUnit($id)
    {
        jf::SQL("SELECT DATE_ADD(Time, INTERVAL 1 DAY) FROM stats WHERE ID=?",
                $id);
    }

    public function regexpOperator($pattern)
    {
        jf::SQL("SELECT ID FROM users WHERE Username REGEXP ?", $pattern);
    }
}

class Catalog
{
    public function groupConcat()
    {
        jf::SQL("SELECT GROUP_CONCAT(Title) AS Path FROM roles");
    }

    public function ifnull($id)
    {
        jf::SQL("SELECT IFNULL(Title, '') FROM roles WHERE ID=?", $id);
    }

    public function unixTimestamp()
    {
        jf::SQL("SELECT UNIX_TIMESTAMP(Time) FROM stats");
    }

    public function fromUnixtime()
    {
        jf::SQL("SELECT FROM_UNIXTIME(Time) FROM stats");
    }

    public function lastInsertId()
    {
        jf::SQL("SELECT LAST_INSERT_ID()");
    }

    public function rand()
    {
        jf::SQL("SELECT ID FROM users ORDER BY RAND() LIMIT 1");
    }

    public function curdate()
    {
        jf::SQL("SELECT ID FROM stats WHERE Time > CURDATE()");
    }

    public function collation()
    {
        jf::SQL("SELECT Username COLLATE utf8_general_ci FROM users ORDER BY 1");
    }

    public function collationInDdl()
    {
        jf::SQL("CREATE TABLE notes ("
                . " ID integer PRIMARY KEY,"
                . " Body text COLLATE utf8_bin NOT NULL)");
    }
}
