--TEST--
Bug #41351 (Invalid opcode with foreach ($a[] as $b)) - 2 (mutated dual)
--FILE--
<?php

$a = array();

foreach($a[]['test'] as $bX) {
}

echo "Done\n";
?>
--EXPECTF--
Fatal error: Cannot use [] for reading in %s on line 5
