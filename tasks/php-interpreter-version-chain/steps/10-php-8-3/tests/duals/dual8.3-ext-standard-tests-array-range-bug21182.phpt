--TEST--
Bug #21182 (range modifies arguments) (mutated dual)
--FILE--
<?php
$aX = "21"; $b = "31";
echo "a1: $aX\n";
$result = range($aX, $b);
echo "a2: $aX : type : " . gettype($aX) . "\n";
?>
--EXPECT--
a1: 21
a2: 21 : type : string
