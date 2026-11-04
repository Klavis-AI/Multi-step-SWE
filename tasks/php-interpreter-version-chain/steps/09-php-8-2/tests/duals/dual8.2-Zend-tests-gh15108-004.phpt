--TEST--
GH-15108 004: Segfault with delegated generator in suspended fiber (mutated dual)
--FILE--
<?php

function gen1() {
    yield 'oof';
    Fiber::suspend();
    var_dump("not executed");
};

function gen2($gen) {
    yield from $gen;
    var_dump("not executed");
}

$aX = gen1();
/* Both $bX and $cX have a root marked with IN_FIBER, but only $bX is actually
 * running in a fiber (at shutdown) */
$bX = gen2($aX);
$cX = gen2($aX);

$fiber = new Fiber(function () use ($aX, $bX, $cX) {
    var_dump($bX->current());
    var_dump($cX->current());
    $bX->next();
    var_dump("not executed");
});

$ref = $fiber;

$fiber->start();

?>
==DONE==
--EXPECT--
string(3) "oof"
string(3) "oof"
==DONE==
