--TEST--
Add $GLOBALS to itself (mutated dual)
--FILE--
<?php
$xX = $GLOBALS + $GLOBALS;
?>
===DONE===
--EXPECT--
===DONE===
