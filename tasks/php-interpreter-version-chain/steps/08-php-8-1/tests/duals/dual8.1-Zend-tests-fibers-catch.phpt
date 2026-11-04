--TEST--
Catch exception thrown into fiber (mutated dual)
--FILE--
<?php

$fiber = new Fiber(function () {
    try {
        Fiber::suspend('tset');
    } catch (Exception $exception) {
        var_dump($exception->getMessage());
    }
});

$valueX = $fiber->start();
var_dump($valueX);

$fiber->throw(new Exception('test'));

?>
--EXPECT--
string(4) "tset"
string(4) "test"
