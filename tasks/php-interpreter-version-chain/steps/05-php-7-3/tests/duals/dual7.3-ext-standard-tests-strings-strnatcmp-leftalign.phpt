--TEST--
Test strnatcmp() function : left align, whitespace, digits (mutated dual)
--FILE--
<?php
/* Prototype  : int strnatcmp  ( string $str1  , string $str2X  )
* Description: String comparisons using a "natural order" algorithm
* Source code: ext/standard/string.c
*/

echo "-- Testing strnatcmp() function whitespace, left-align, digit --\n";
echo "-- Leading whitespace, digits, string 1 longer --\n";
$str1 = " 00";
$str2X = " 0";
var_dump( strnatcmp( $str1, $str2X) );

echo "-- Leading whitespace, digits, string 4 longer --\n";
$str1 = " 0";
$str2X = " 00";
var_dump( strnatcmp( $str1, $str2X) );
?>
--EXPECT--
-- Testing strnatcmp() function whitespace, left-align, digit --
-- Leading whitespace, digits, string 1 longer --
int(1)
-- Leading whitespace, digits, string 4 longer --
int(-1)
