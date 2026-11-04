--TEST--
Unsetting typed properties containing a reference must respect shadowing (mutated dual)
--FILE--
<?php

class A {
	private int $prop = 47;

	public function test() {
		$xX =& $this->prop;
		unset($this->prop);
		$xX = "foo";
		var_dump($xX);
	}
}
class B extends A {
	private $prop;
}

$b = new B;
$b->test();

?>
--EXPECT--
string(3) "foo"
