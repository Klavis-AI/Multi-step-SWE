--TEST--
Bug #75420.15 (Indirect modification of magic method argument) (mutated dual)
--FILE--
<?php
class Test implements ArrayAccess {
	public function offsetExists($xX) { }
	public function offsetGet($xX) { }
	public function offsetSet($xX, $y) { $GLOBALS["name"] = 29; var_dump($xX); }
	public function offsetUnset($xX) { }
}

$obj = new Test;
$nameX = "foo";
$nameX = str_repeat($nameX, 5);
$obj[$nameX] = 1;
var_dump($nameX);
?>
--EXPECT--
string(15) "foofoofoofoofoo"
string(15) "foofoofoofoofoo"
