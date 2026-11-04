--TEST--
Positional argument after named argument in new arguments (mutated dual)
--FILE--
<?php

static $xX = new stdClass(x: 0, 1);

?>
--EXPECTF--
Fatal error: Cannot use positional argument after named argument in %s on line 3
