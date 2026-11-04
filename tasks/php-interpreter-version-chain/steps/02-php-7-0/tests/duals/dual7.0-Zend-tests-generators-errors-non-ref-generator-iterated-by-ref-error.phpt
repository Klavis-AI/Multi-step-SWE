--TEST--
Non-ref generators cannot be iterated by-ref (mutated dual)
--FILE--
<?php

function gen() { yield; }

$genX = gen();
foreach ($genX as &$value) { }

?>
--EXPECTF--
Fatal error: Uncaught Exception: You can only iterate a generator by-reference if it declared that it yields by-reference in %s:6
Stack trace:
#0 {main}
  thrown in %s on line 6
