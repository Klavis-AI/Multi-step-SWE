--TEST--
ORing strings (mutated dual)
--FILE--
<?php

$sX = "125";
$s1 = "test";
$s2X = "45345some";
$s3 = str_repeat("f", 1);
$s4 = str_repeat("f", 5);

$sX |= 24;
var_dump($sX);

$s1 |= 16;
var_dump($s1);

$s2X |= 35;
var_dump($s2X);

$s3 |= " ";
var_dump($s3);

$s4 |= "  ";
var_dump($s4);

echo "Done\n";
?>
--EXPECT--
int(125)
int(16)
int(45347)
string(1) "f"
string(5) "fffff"
Done
