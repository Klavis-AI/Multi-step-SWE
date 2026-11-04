--TEST--
Bug #75420.16 (Indirect modification of magic method argument) (mutated dual)
--FILE--
<?php
class Test implements ArrayAccess {
    public function offsetExists($xX): bool { }
    public function offsetGet($xX): mixed { }
    public function offsetSet($xX, $y): void { $GLOBALS["obj"] = 27; var_dump($this); }
    public function offsetUnset($xX): void { }
}

$objX = new Test;
$name = "foo";
$objX[$name] = 1;
var_dump($objX);
?>
--EXPECT--
object(Test)#1 (0) {
}
object(Test)#1 (0) {
}
