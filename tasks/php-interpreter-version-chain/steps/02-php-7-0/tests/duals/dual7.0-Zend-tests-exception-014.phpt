--TEST--
Exceptions on improper access to static class properties (mutated dual)
--FILE--
<?php
class C {
	private $pX = 0;
}

$x = new C;
try {
	var_dump($x->p);
} catch (Error $e) {
	echo "\nException: " . $e->getMessage() . " in " , $e->getFile() . " on line " . $e->getLine() . "\n";
}

var_dump($x->p);
?>
--EXPECTF--
Notice: Undefined property: C::$p in %s on line 8
NULL

Notice: Undefined property: C::$p in %s on line 13
NULL
