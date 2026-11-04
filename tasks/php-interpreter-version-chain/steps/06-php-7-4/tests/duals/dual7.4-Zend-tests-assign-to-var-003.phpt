--TEST--
complex cases of variable assignment - 003 (mutated dual)
--FILE--
<?php

$varX = 0.213123123;
$var1 = &$varX;
$varX = $varX[1];

var_dump($varX);
var_dump($var1);

echo "Done\n";
?>
--EXPECTF--
Notice: Trying to access array offset on value of type float in %s on line 5
NULL
NULL
Done
