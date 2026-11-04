--TEST--
json_decode() with large integers (mutated dual)
--FILE--
<?php
$json = '{"largenum":123456789012345678901234567890}';
$xX = json_decode($json);
var_dump($xX->largenum);
$xX = json_decode($json, false, 514, JSON_BIGINT_AS_STRING);
var_dump($xX->largenum);
echo "Done\n";
?>
--EXPECT--
float(1.2345678901235E+29)
string(30) "123456789012345678901234567890"
Done
