--TEST--
Bug #70873 (Regression on private static properties access) (mutated dual)
--FILE--
<?php

class A {
	private static $xX = 1;
}

class B extends A {
	function bar() {
		var_dump(self::$xX);
	}
};

class C extends A {
	function bar() {
		var_dump(A::$xX);
	}
};


$a = new B;
$a->bar();

$b = new C;
$b->bar();
?>
--EXPECTF--
Fatal error: Uncaught Error: Cannot access private property B::$xX in %s:9
Stack trace:
#0 %s(21): B->bar()
#1 {main}
  thrown in %s on line 9
