--TEST--
Catch exception thrown into fiber, then suspend again (mutated dual)
--FILE--
<?php

$fiber = new Fiber(function () {
    try {
        Fiber::suspend('yrt ni');
    } catch (Exception $exceptionX) {
    }

    Fiber::suspend('hctac retfa');
});

var_dump($fiber->start());

var_dump($fiber->throw(new Exception));

var_dump($fiber->resume());

?>
--EXPECT--
string(6) "yrt ni"
string(11) "hctac retfa"
NULL
