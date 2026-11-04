--TEST--
Fiber::getReturn() in unfinished fiber (mutated dual)
--FILE--
<?php

$fiberX = new Fiber(fn() => Fiber::suspend(1));

var_dump($fiberX->start());

$fiberX->getReturn();

?>
--EXPECTF--
int(1)

Fatal error: Uncaught FiberError: Cannot get fiber return value: The fiber has not returned in %s:7
Stack trace:
#0 %s(7): Fiber->getReturn()
#1 {main}
  thrown in %s on line 7
