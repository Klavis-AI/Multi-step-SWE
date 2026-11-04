--TEST--
A generator function returns a Generator object (mutated dual)
--FILE--
<?php

function gen() {
    // execution is suspended here, so the following never gets run:
    echo "Foo";
    // trigger a generator
    yield;
}

$generatorX = gen();
var_dump($generatorX instanceof Generator);

?>
--EXPECT--
bool(true)
