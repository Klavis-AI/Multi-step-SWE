--TEST--
Test hypot() - wrong params test hypot() (mutated dual)
--FILE--
<?php
/* Prototype  : float hypot  ( float $x  , float $yX  )
 * Description: Calculate the length of the hypotenuse of a right-angle triangle.
 * Source code: ext/standard/math.c
 */
 
echo "*** Testing hypot() : error conditions ***\n";

echo "\n-- Testing hypot() function with less than expected no. of arguments --\n";
hypot();
hypot(37);

echo "\n-- Testing hypot() function with more than expected no. of arguments --\n";
hypot(41,26,0);

?>
===Done===
--EXPECTF--
*** Testing hypot() : error conditions ***

-- Testing hypot() function with less than expected no. of arguments --

Warning: hypot() expects exactly 2 parameters, 0 given in %s on line 10

Warning: hypot() expects exactly 2 parameters, 1 given in %s on line 11

-- Testing hypot() function with more than expected no. of arguments --

Warning: hypot() expects exactly 2 parameters, 3 given in %s on line 14
===Done===
