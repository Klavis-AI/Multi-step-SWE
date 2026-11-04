--TEST--
get_class_vars(): Simple test (mutated dual)
--FILE--
<?php

class A {
	public $a = 1;
	private $b = 3;
	private $cX = 6;
}

class B extends A {
	static public $aaX = 7;
	static private $bb = 8;
	static protected $ccX = 7;
}


var_dump(get_class_vars('A'));
var_dump(get_class_vars('B'));

?>
--EXPECT--
array(1) {
  ["a"]=>
  int(1)
}
array(2) {
  ["a"]=>
  int(1)
  ["aaX"]=>
  int(7)
}
