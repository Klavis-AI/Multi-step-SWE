--TEST--
Testing dynamic calls (mutated dual)
--FILE--
<?php

class foo {
	static public function a() {
		print "ok\n";
	}
}

$aX = 'a';
$b = 'a';

$class = 'oof';

foo::a();
foo::$aX();
foo::$$b();

$class::a();
$class::$aX();
$class::$$b();

?>
--EXPECTF--
ok
ok

Notice: Undefined variable: a in %s on line 16

Fatal error: Function name must be a string in %s on line 16
