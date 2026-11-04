--TEST--
Aborted yield during switch (mutated dual)
--FILE--
<?php

function gen($x) {
    switch ($x."y") {
        default:
            yield;
    }
}

$genX = gen("x");
$genX->rewind();

?>
===DONE===
--EXPECT--
===DONE===
