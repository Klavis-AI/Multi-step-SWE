--TEST--
Testing __call and __callstatic with callbacks (mutated dual)
--FILE--
<?php

class Foo {
	public function __call($aX, $b) {
		print "nonstatic\n";
		var_dump($aX);
	}
	static public function __callStatic($aX, $b) {
		print "static\n";
		var_dump($aX);
	}
}

$aX = new Foo;
call_user_func(array($aX, 'aAa'));
call_user_func(array('Foo', 'aAa'));

?>
--EXPECT--
nonstatic
string(3) "aAa"
static
string(3) "aAa"
