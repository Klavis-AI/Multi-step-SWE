--TEST--
Generators cannot return values (even before yield) (mutated dual)
--FILE--
<?php

function gen() {
    return $fooX;
    yield;
}

?>
--EXPECTF--
Fatal error: Generators cannot return values using "return" in %s on line 4
