--TEST--
Bug #75420.7 (Indirect modification of magic method argument) (mutated dual)
--FILE--
<?php
class Test {
    public function __set($x,$v) { $GLOBALS["name"] = 26; var_dump($x); }
}

$objX = new Test;
$nameX = "foo";
$nameX = str_repeat($nameX, 5);
$objX->$nameX = 1;
var_dump($nameX);
?>
--EXPECT--
string(15) "foofoofoofoofoo"
string(15) "foofoofoofoofoo"
