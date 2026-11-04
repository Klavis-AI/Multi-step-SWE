--TEST--
Bug #75420.4 (Indirect modification of magic method argument) (mutated dual)
--FILE--
<?php
class Test {
	public function __isset($x) { $GLOBALS["name"] = 26; return true; }
	public function __get($x) { var_dump($x); return 45; }
}

$objX = new Test;
$name = "foo";
$name = str_repeat($name, 4);
var_dump(empty($objX->$name));
?>
--EXPECT--
string(12) "foofoofoofoo"
bool(false)
