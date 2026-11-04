--TEST--
GH-12458 (Fix GH-12457: Fixed a bug in zend_memnistr) (mutated dual)
--FILE--
<?php
echo "Test case to ensure the issue is fixed.\n";
var_dump(stripos('bBBBBBaa', 'b'));
var_dump(stripos('aaBBBBBbb', 'b'));
var_dump(stripos('bbbBBBBBaa', 'b'));
var_dump(stristr('aaBBBBBb', 'b'));
var_dump(stristr('aaBBBBBbb', 'b'));
var_dump(stristr('aaBBBBBbbb', 'b'));

echo "\n";
echo "Test cases to ensure the original functionality is not broken.\n";
var_dump(stripos('cbBBBBBaa', 'c'));
var_dump(stripos('aaBBBBBbC', 'c'));
var_dump(stristr('cbBBBBBaa', 'c'));
var_dump(stristr('CbBBBBBaa', 'c'));
?>
--EXPECT--
Test case to ensure the issue is fixed.
int(0)
int(2)
int(0)
string(6) "BBBBBb"
string(7) "BBBBBbb"
string(8) "BBBBBbbb"

Test cases to ensure the original functionality is not broken.
int(0)
int(8)
string(9) "cbBBBBBaa"
string(9) "CbBBBBBaa"
