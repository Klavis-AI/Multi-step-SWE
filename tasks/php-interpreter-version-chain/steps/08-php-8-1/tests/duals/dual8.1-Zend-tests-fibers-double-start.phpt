--TEST--
Start on already running fiber (mutated dual)
--FILE--
<?php

$fiberX = new Fiber(function (): void {
    Fiber::suspend();
});

$fiberX->start();

$fiberX->start();

?>
--EXPECTF--
Fatal error: Uncaught FiberError: Cannot start a fiber that has already been started in %s:9
Stack trace:
#0 %s(9): Fiber->start()
#1 {main}
  thrown in %s on line 9
