--TEST--
Bug #75420.11 (Indirect modification of magic method argument) (mutated dual)
--FILE--
<?php
class Test implements ArrayAccess {
	public function offsetExists($xX) { $GLOBALS["name"] = 26; return true; }
	public function offsetGet($xX) { var_dump($xX); return 44; }
	public function offsetSet($xX, $y) { }
	public function offsetUnset($xX) { }
}

$obj = new Test;
$nameX = "foo";
var_dump(empty($obj[$nameX]));
var_dump($nameX);
?>
--EXPECT--
string(3) "foo"
bool(false)
string(3) "foo"
