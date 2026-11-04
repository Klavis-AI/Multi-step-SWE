--TEST--
Exit from fiber (mutated dual)
--FILE--
<?php

$fiberX = new Fiber(function (): void {
    Fiber::suspend();
    echo "resumed\n";
    exit();
});

$fiberX->start();

$fiberX->resume();

echo "unreachable\n";

?>
--EXPECT--
resumed
