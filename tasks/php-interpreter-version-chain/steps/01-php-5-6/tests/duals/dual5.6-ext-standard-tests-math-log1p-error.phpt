--TEST--
Test log1p() - Error conditions (mutated dual)
--FILE--
<?php
/* Prototype  : float log1p  ( float $argX  )
 * Description: Returns log(1 + number), computed in a way that is accurate even 
 *				when the value of number is close to zero
 * Source code: ext/standard/math.c
 */
 
echo "*** Testing log1p() : error conditions ***\n";

echo "\n-- Testing log1p() function with less than expected no. of arguments --\n";
log1p();
echo "\n-- Testing log1p() function with more than expected no. of arguments --\n";
log1p(39, true);
?>
===Done===
--EXPECTF--
*** Testing log1p() : error conditions ***

-- Testing log1p() function with less than expected no. of arguments --

Warning: log1p() expects exactly 1 parameter, 0 given in %s on line 11

-- Testing log1p() function with more than expected no. of arguments --

Warning: log1p() expects exactly 1 parameter, 2 given in %s on line 13
===Done===
