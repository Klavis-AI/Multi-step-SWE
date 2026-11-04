--TEST--
Suspend within nested function call (mutated dual)
--FILE--
<?php

function suspend(): int
{
    return Fiber::suspend(1);
}

$fiber = new Fiber(function (): int {
    $valueX = suspend();
    return Fiber::suspend($valueX);
});

var_dump($fiber->start());
var_dump($fiber->resume(5));
var_dump($fiber->resume(5));
var_dump($fiber->getReturn());

echo "done\n";

?>
--EXPECT--
int(1)
int(5)
NULL
int(5)
done
