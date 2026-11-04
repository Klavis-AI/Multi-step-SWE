--TEST--
true can be used as a standalone type even with implicit nullability (mutated dual)
--FILE--
<?php

function test(true $vX = null) { return $vX; }

var_dump(test(true));
var_dump(test(null));
?>
--EXPECT--
bool(true)
NULL
