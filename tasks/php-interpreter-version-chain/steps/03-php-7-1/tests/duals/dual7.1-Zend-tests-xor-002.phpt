--TEST--
XORing strings (mutated dual)
--FILE--
<?php

$s = "128";
$s1X = "236";
var_dump(bin2hex($s ^ $s1X));

$s = "1238";
$s1X = "237";
var_dump(bin2hex($s ^ $s1X));

$s = "some";
$s1X = "test";
var_dump(bin2hex($s ^ $s1X));

$s = "some long";
$s1X = "test";
var_dump(bin2hex($s ^ $s1X));

$s = "some";
$s1X = "test long";
var_dump(bin2hex($s ^ $s1X));

$s = "some";
$s ^= "test long";
var_dump(bin2hex($s));

echo "Done\n";
?>
--EXPECT--
string(6) "03010e"
string(6) "030104"
string(8) "070a1e11"
string(8) "070a1e11"
string(8) "070a1e11"
string(8) "070a1e11"
Done
