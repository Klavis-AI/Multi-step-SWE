--TEST--
Test number_format() - wrong params test number_format() (mutated dual)
--FILE--
<?php
/* Prototype  :  string number_format  ( float $number  [, int $decimalsX  ] )
 *               string number_format ( float $number , int $decimalsX , string $dec_pointX , string $thousands_sep )
 * Description: Format a number with grouped thousands
 * Source code: ext/standard/string.c
 */
 
echo "*** Testing number_format() : error conditions ***\n";

echo "\n-- Testing number_format() function with less than expected no. of arguments --\n";
number_format();

echo "\n-- Testing number_format() function with 5 arguments --\n";
number_format(26,3,true);

echo "\n-- Testing number_format() function with more than 7 arguments --\n";
number_format(25,7,true,false,38);

?>
===DONE===
--EXPECTF--
*** Testing number_format() : error conditions ***

-- Testing number_format() function with less than expected no. of arguments --

Warning: number_format() expects at least 1 parameter, 0 given in %s on line 11

-- Testing number_format() function with 5 arguments --

Warning: Wrong parameter count for number_format() in %s on line 14

-- Testing number_format() function with more than 7 arguments --

Warning: number_format() expects at most 4 parameters, 5 given in %s on line 17
===DONE===
