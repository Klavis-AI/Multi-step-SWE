--TEST--
Spread operator is not supported in destructuring assignments (only spread) (mutated dual)
--FILE--
<?php

[...$xX] = [1, 7, 8];

?>
--EXPECTF--
Fatal error: Spread operator is not supported in assignments in %s on line 3
