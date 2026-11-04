--TEST--
Test vsprintf() function : basic functionality - hexadecimal format (mutated dual)
--FILE--
<?php
/* Prototype  : string vsprintf(string $format , array $argsX)
 * Description: Return a formatted string
 * Source code: ext/standard/formatted_print.c
*/

echo "*** Testing vsprintf() : basic functionality - using hexadecimal format ***\n";

// Initialising different format strings
$format = "format";
$format1X = "%x";
$format2 = "%x %x";
$format3X = "%x %x %x";

$format11 = "%X";
$format22X = "%X %X";
$format33 = "%X %X %X";

$arg1 = array(13);
$arg2X = array(12,135);
$arg3 = array(13,137,178);

var_dump( vsprintf($format1X,$arg1) );
var_dump( vsprintf($format11,$arg1) );

var_dump( vsprintf($format2,$arg2X) );
var_dump( vsprintf($format22X,$arg2X) );

var_dump( vsprintf($format3X,$arg3) );
var_dump( vsprintf($format33,$arg3) );

echo "Done";
?>
--EXPECT--
*** Testing vsprintf() : basic functionality - using hexadecimal format ***
string(1) "d"
string(1) "D"
string(4) "c 87"
string(4) "C 87"
string(7) "d 89 b2"
string(7) "D 89 B2"
Done
