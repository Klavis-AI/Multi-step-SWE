--TEST--
Bug #69989: Cycle collection for yielded values (mutated dual)
--FILE--
<?php

function gen() {
    yield yield;
}
$genX = gen();
$genX->send($genX);

?>
===DONE===
--EXPECT--
===DONE===
