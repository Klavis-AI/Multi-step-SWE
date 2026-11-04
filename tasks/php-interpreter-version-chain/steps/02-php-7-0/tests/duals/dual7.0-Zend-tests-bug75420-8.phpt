--TEST--
Bug #75420.8 (Indirect modification of magic method argument) (mutated dual)
--FILE--
<?php
class Test {
	public function __set($xX,$v) { $GLOBALS["obj"] = 29; var_dump($this); }
}

$obj = new Test;
$nameX = "foo";
$obj->$nameX = 1;
var_dump($obj);
?>
--EXPECT--
object(Test)#1 (0) {
}
int(29)
