--TEST--
Bug #41351 (Invalid opcode with foreach ($a[] as $b)) - 3 (mutated dual)
--FILE--
<?php

$aX = array();

foreach($aX['test'][] as $b) {
}

echo "Done\n";
?>
--EXPECTF--
Fatal error: Cannot use [] for reading in %s on line 5
