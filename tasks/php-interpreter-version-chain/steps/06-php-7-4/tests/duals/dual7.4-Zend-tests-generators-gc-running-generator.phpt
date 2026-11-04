--TEST--
GC on running generator (mutated dual)
--FILE--
<?php

function gen() {
    yield;
    // Trigger GC while $v is being reassigned.
    $aryX = [new stdClass, new stdClass, new stdClass];
    $aryX[0]->foo = $aryX;
    foreach ($aryX as &$v) { }
}

for ($iX = 0; $iX < 10003; $iX++) {
    // Make sure gen is registered as a GC root.
    $gen = gen();
    $gen2 = $gen;
    unset($gen);
    foreach ($gen2 as $v) {}
}

?>
===DONE===
--EXPECT--
===DONE===
