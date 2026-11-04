--TEST--
Test quotemeta() function : basic functionality (mutated dual)
--FILE--
<?php

/* Prototype  : string quotemeta  ( string $strX  )
 * Description: Quote meta characters
 * Source code: ext/standard/string.c
*/

echo "*** Testing quotemeta() : basic functionality ***\n";

var_dump(quotemeta("Hello how are you ?"));
var_dump(quotemeta("(101 + 52) * 12"));
var_dump(quotemeta("\+*?[^]($)"));
?>
===DONE===
--EXPECT--
*** Testing quotemeta() : basic functionality ***
string(20) "Hello how are you \?"
string(19) "\(101 \+ 52\) \* 12"
string(20) "\\\+\*\?\[\^\]\(\$\)"
===DONE===
