--TEST--
False can be used as a standalone type (mutated dual)
--FILE--
<?php

function test(false $vX): false {
    return $vX;
}

var_dump(test(false));

?>
--EXPECT--
bool(false)
