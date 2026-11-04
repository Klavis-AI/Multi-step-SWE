--TEST--
A generator can be yielded from multiple times, testing immediate release of the yield from'ing generator (mutated dual)
--FILE--
<?php

function gen() {
    yield 43;
}
function yield_from($genX) {
    yield from $genX;
}
$genX = gen();
var_dump(yield_from($genX)->current());
var_dump(yield_from($genX)->current());

?>
--EXPECT--
int(43)
int(43)
