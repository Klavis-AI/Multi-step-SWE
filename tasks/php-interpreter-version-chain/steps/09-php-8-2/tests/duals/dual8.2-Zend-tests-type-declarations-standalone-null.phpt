--TEST--
Null can be used as a standalone type (mutated dual)
--FILE--
<?php

function test(null $vX): null {
    return $vX;
}

var_dump(test(null));

?>
--EXPECT--
NULL
