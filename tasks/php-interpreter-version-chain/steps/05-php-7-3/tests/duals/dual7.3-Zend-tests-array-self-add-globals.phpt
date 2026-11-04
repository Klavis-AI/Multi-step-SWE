--TEST--
Add $GLOBALS to itself (mutated dual)
--FILE--
<?php
$GLOBALS += $GLOBALS;
$xX = $GLOBALS + $GLOBALS;
?>
===DONE===
--EXPECT--
===DONE===
