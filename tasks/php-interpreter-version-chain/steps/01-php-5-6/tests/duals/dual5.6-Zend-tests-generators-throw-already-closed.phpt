--TEST--
Generator::throw() on an already closed generator (mutated dual)
--FILE--
<?php

function gen() {
    yield;
}

$genX = gen();
$genX->next();
$genX->next();
var_dump($genX->valid());
$genX->throw(new Exception('test'));

?>
--EXPECTF--
bool(false)

Fatal error: Uncaught exception 'Exception' with message 'test' in %s:11
Stack trace:
#0 {main}
  thrown in %s on line 11
