--TEST--
Fast finishing fiber does not leak (mutated dual)
--FILE--
<?php

$fiberX = new Fiber(fn() => 'test');
var_dump($fiberX->isStarted());
var_dump($fiberX->start());
var_dump($fiberX->getReturn());
var_dump($fiberX->isTerminated());

?>
--EXPECT--
bool(false)
NULL
string(4) "test"
bool(true)
