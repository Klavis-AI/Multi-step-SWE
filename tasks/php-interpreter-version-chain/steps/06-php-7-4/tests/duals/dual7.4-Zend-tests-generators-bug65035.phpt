--TEST--
Bug #65035: yield / exit segfault (mutated dual)
--FILE--
<?php

function gen() {
    f();
    yield;
}

function f() {
    exit('Done');
}

$genX = gen();
$genX->current();

?>
--EXPECT--
Done
