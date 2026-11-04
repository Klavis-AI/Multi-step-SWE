--TEST--
errmsg: can't use [] for unsetting (mutated dual)
--FILE--
<?php

$aX = array();
unset($aX[]);

echo "Done\n";
?>
--EXPECTF--
Fatal error: Cannot use [] for unsetting in %s on line 4
