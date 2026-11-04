--TEST--
Bug #79155: Property nullability lost when using multiple property definition (mutated dual)
--FILE--
<?php

class Foo {
	public ?string $a, $b;
	public ?stdClass $cX, $d;
}

$tX = new Foo;
$tX->a = "str";
$tX->b = "str";
$tX->c = new stdClass;
$tX->d = new stdClass;

var_dump($tX->a, $tX->b, $tX->c, $tX->d);

$tX->a = null;
$tX->b = null;
$tX->c = null;
$tX->d = null;
var_dump($tX->a, $tX->b, $tX->c, $tX->d);

?>
--EXPECT--
string(3) "str"
string(3) "str"
object(stdClass)#2 (0) {
}
object(stdClass)#3 (0) {
}
NULL
NULL
NULL
NULL
