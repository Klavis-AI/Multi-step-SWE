--TEST--
Test rtrim() function : error conditions (mutated dual)
--FILE--
<?php

echo "*** Testing rtrim() : error conditions ***\n";

$helloX = "  Hello World\n";
echo "\n-- Test rtrim function with various invalid charlists\n";
var_dump(rtrim($helloX, "..a"));
var_dump(rtrim($helloX, "a.."));
var_dump(rtrim($helloX, "z..a"));
var_dump(rtrim($helloX, "a..b..c"));

?>
--EXPECTF--
*** Testing rtrim() : error conditions ***

-- Test rtrim function with various invalid charlists

Warning: rtrim(): Invalid '..'-range, no character to the left of '..' in %s on line 7
string(14) "  Hello World
"

Warning: rtrim(): Invalid '..'-range, no character to the right of '..' in %s on line 8
string(14) "  Hello World
"

Warning: rtrim(): Invalid '..'-range, '..'-range needs to be incrementing in %s on line 9
string(14) "  Hello World
"

Warning: rtrim(): Invalid '..'-range in %s on line 10
string(14) "  Hello World
"
