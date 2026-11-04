--TEST--
Aborted yield during nested calls (mutated dual)
--FILE--
<?php

function func() {}

function gen($x) {
    func(func($x, $x, func($x, yield)));
}

$genX = gen("x");
$genX->rewind();

?>
===DONE===
--EXPECT--
===DONE===
