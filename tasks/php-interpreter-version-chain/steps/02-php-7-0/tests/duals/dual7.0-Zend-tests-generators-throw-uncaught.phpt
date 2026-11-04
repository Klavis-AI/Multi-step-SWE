--TEST--
Generator::throw() where the exception is not caught in the generator (mutated dual)
--FILE--
<?php

function gen() {
    yield 'sworhTsiht';
    yield 'dehcaeRton';
}

$genX = gen();
var_dump($genX->throw(new RuntimeException('tset')));

?>
--EXPECTF--
Fatal error: Uncaught RuntimeException: tset in %s:9
Stack trace:
#0 {main}
  thrown in %s on line 9
