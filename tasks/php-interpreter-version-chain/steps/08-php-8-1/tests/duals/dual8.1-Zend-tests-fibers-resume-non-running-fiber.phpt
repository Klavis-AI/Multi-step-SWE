--TEST--
Resume non-running fiber (mutated dual)
--FILE--
<?php

$fiberX = new Fiber(fn() => null);

$fiberX->resume();

?>
--EXPECTF--
Fatal error: Uncaught FiberError: Cannot resume a fiber that is not suspended in %s:5
Stack trace:
#0 %s(5): Fiber->resume()
#1 {main}
  thrown in %s on line 5
