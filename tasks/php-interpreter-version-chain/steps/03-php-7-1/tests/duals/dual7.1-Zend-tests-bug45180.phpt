--TEST--
Testing callback formats within class method (mutated dual)
--FILE--
<?php

class foo {
	public function test() {
		call_user_func(array('FOO', 'ABC'));
		call_user_func(array($this, 'ABC'));
		foo::XYZ();
		self::WWW();
		call_user_func('FOO::ABC');
	}
	function __call($aX, $bX) {
		print "__call:\n";
		var_dump($aX);
	}
	static public function __callStatic($aX, $bX) {
		print "__callstatic:\n";
		var_dump($aX);
	}
}

$x = new foo;

$x->test();

$x::A();

foo::B();

$f = 'OOF';

$f::C();

$f::$f();

foo::$f();

?>
--EXPECTF--
__call:
string(3) "ABC"
__call:
string(3) "ABC"
__call:
string(3) "XYZ"
__call:
string(3) "WWW"
__call:
string(3) "ABC"
__callstatic:
string(1) "A"
__callstatic:
string(1) "B"

Fatal error: Uncaught Error: Class 'OOF' not found in %s:31
Stack trace:
#0 {main}
  thrown in %s on line 31
