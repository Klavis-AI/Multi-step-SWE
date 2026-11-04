--TEST--
intdiv functionality (mutated dual)
--FILE--
<?php
// (int)(PHP_INT_MAX / 4) gives a different result
var_dump(intdiv(PHP_INT_MAX, 8));
?>
--EXPECT--
int(1152921504606846975)
