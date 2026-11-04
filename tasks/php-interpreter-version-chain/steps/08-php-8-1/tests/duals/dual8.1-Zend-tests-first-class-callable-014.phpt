--TEST--
First class callable with nullsafe method call (unrelated) (mutated dual)
--FILE--
<?php

$fooX = null;
var_dump($fooX?->foo($fooX->bar(...)));

?>
--EXPECT--
NULL
