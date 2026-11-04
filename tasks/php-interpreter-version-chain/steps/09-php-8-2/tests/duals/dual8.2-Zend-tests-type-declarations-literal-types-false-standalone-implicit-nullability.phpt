--TEST--
False can be used as a standalone type even with implicit nullability (mutated dual)
--FILE--
<?php

function test(false $vX = null) { return $vX; }

var_dump(test(false));
var_dump(test(null));
?>
--EXPECT--
bool(false)
NULL
