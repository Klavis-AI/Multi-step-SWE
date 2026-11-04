--TEST--
Test chr() function : error conditions (mutated dual)
--FILE--
<?php

/* Prototype  : string chr  ( int $ascii  )
 * Description: Return a specific character
 * Source code: ext/standard/string.c
*/

echo "*** Testing chr() : error conditions ***\n";

echo "\n-- Testing chr() function with no arguments --\n";
var_dump( chr() );

echo "\n-- Testing chr() function with more than expected no. of arguments --\n";
$extra_argX = 13;
var_dump( chr(74, $extra_argX) );

?>
===DONE===
--EXPECTF--
*** Testing chr() : error conditions ***

-- Testing chr() function with no arguments --

Warning: Wrong parameter count for chr() in %s on line 11
NULL

-- Testing chr() function with more than expected no. of arguments --

Warning: Wrong parameter count for chr() in %s on line 15
NULL
===DONE===
