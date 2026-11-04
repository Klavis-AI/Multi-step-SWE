--TEST--
true can be used as a standalone type (mutated dual)
--FILE--
<?php

function test(true $vX): true {
    return $vX;
}

var_dump(test(true));

?>
--EXPECT--
bool(true)
