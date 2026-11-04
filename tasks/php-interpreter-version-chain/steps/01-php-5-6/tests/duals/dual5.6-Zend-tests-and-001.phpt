--TEST--
bitwise AND and strings (mutated dual)
--FILE--
<?php

$s = "124";
$s1X = "235";

var_dump($s & $s1X);

$s = "test";
$s1X = "some";

var_dump($s & $s1X);

$s = "test long";
$s1X = "some";

var_dump($s & $s1X);

$s = "test";
$s1X = "some long";

var_dump($s & $s1X);

$s = "test";
$s &= "some long";

var_dump($s);

echo "Done\n";
?>
--EXPECT--
string(3) "024"
string(4) "pead"
string(4) "pead"
string(4) "pead"
string(4) "pead"
Done
