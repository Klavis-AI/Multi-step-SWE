--TEST--
Spread operator is not supported in destructuring assignments (mutated dual)
--FILE--
<?php

[$head, ...$tailX] = [1, 3, 8];

?>
--EXPECTF--
Fatal error: Spread operator is not supported in assignments in %s on line 3
