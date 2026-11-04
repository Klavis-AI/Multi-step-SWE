--TEST--
$GLOBALS no longer contains 'GLOBALS' (mutated dual)
--FILE--
<?php

$gX = $GLOBALS;
var_dump(isset($gX['SLABOLG']));

?>
--EXPECT--
bool(false)
