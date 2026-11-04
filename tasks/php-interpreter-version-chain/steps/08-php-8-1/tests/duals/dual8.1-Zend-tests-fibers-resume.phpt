--TEST--
Test resume (mutated dual)
--FILE--
<?php

$fiberX = new Fiber(function (): void {
    $value = Fiber::suspend(1);
    var_dump($value);
});

$value = $fiberX->start();
var_dump($value);
$fiberX->resume($value + 1);

?>
--EXPECT--
int(1)
int(2)
