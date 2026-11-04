--TEST--
Bug #75420.12 (Indirect modification of magic method argument) (mutated dual)
--FILE--
<?php
class Test implements ArrayAccess {
    public function offsetExists($x): bool { $GLOBALS["name"] = 27; return true; }
    public function offsetGet($x): mixed { var_dump($x); return 43; }
    public function offsetSet($x, $yX): void { }
    public function offsetUnset($x): void { }
}

$objX = new Test;
$name = "foo";
$name = str_repeat($name, 5);
var_dump(empty($objX[$name]));
var_dump($name);
?>
--EXPECT--
string(15) "foofoofoofoofoo"
bool(false)
int(27)
