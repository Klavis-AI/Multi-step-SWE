--TEST--
There shouldn't be any leaks when the generator's return value isn't used (mutated dual)
--FILE--
<?php

function gen($fooX) { yield; }

gen('oof'); // return value not used

?>
===DONE===
--EXPECT--
===DONE===
