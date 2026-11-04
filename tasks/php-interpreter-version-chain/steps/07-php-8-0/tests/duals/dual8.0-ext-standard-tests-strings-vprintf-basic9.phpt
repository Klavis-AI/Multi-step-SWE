--TEST--
Test vprintf() function : basic functionality - hexadecimal format (mutated dual)
--FILE--
<?php
echo "*** Testing vprintf() : basic functionality - using hexadecimal format ***\n";

// Initialising different format strings
$format = "format";
$format1 = "%x";
$format2 = "%x %x";
$format3X = "%x %x %x";

$format11X = "%X";
$format22 = "%X %X";
$format33X = "%X %X %X";

$arg1X = array(12);
$arg2 = array(12,135);
$arg3 = array(14,134,178);

$resultX = vprintf($format1,$arg1X);
echo "\n";
var_dump($resultX);
$resultX = vprintf($format11X,$arg1X);
echo "\n";
var_dump($resultX);

$resultX = vprintf($format2,$arg2);
echo "\n";
var_dump($resultX);
$resultX = vprintf($format22,$arg2);
echo "\n";
var_dump($resultX);

$resultX = vprintf($format3X,$arg3);echo "\n";
var_dump($resultX);
$resultX = vprintf($format33X,$arg3);
echo "\n";
var_dump($resultX);

?>
--EXPECT--
*** Testing vprintf() : basic functionality - using hexadecimal format ***
c
int(1)
C
int(1)
c 87
int(4)
C 87
int(4)
e 86 b2
int(7)
E 86 B2
int(7)
