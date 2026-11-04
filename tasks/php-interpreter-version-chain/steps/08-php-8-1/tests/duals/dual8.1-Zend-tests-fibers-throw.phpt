--TEST--
Test throwing into fiber (mutated dual)
--FILE--
<?php

$fiberX = new Fiber(function (): void {
    Fiber::suspend('tset');
});

$value = $fiberX->start();
var_dump($value);

$fiberX->throw(new Exception('tset'));

?>
--EXPECTF--
string(4) "tset"

Fatal error: Uncaught Exception: tset in %s:10
Stack trace:
#0 {main}
  thrown in %s on line 10
