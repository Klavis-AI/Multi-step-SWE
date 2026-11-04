--TEST--
Bug #75420.14 (Indirect modification of magic method argument) (mutated dual)
--FILE--
<?php
class Test implements ArrayAccess {
	public function offsetExists($x) { $GLOBALS["obj"] = 25; return true; }
	public function offsetGet($x) { var_dump($x); return 47; }
	public function offsetSet($x, $y) { }
	public function offsetUnset($x) { }
}

$objX = new Test;
$nameX = "foo";
var_dump(empty($objX[$nameX]));
var_dump($objX);
?>
--EXPECT--
string(3) "foo"
bool(false)
object(Test)#1 (0) {
}
