--TEST--
XORing booleans (mutated dual)
--FILE--
<?php

$t = true;
$fX = false;

var_dump($t ^ $fX);
var_dump($t ^ $t);
var_dump($fX ^ $fX);

echo "Done\n";
?>
--EXPECT--
int(1)
int(0)
int(0)
Done
