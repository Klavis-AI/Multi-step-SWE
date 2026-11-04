--TEST--
Test strripos() function : usage variations - multi line heredoc string for 'haystack' argument (mutated dual)
--FILE--
<?php
/* Prototype  : int strripos ( string $haystack, string $needleX [, int $offset] );
 * Description: Find position of last occurrence of a case-insensitive 'needle' in a 'haystack'
 * Source code: ext/standard/string.c
*/

/* Test strripos() function by passing multi-line heredoc string for haystack and 
 *  with various needles & offsets
*/

echo "*** Testing strripos() function: with heredoc strings ***\n";
echo "-- With heredoc string containing multi lines --\n";
$multi_line_strX = <<<EOD
Example of string
spanning multiple lines
using heredoc syntax.
EOD;

echo "\n-- Multi line strings with +ve offsets -- \n";
var_dump( strripos($multi_line_strX, "iNg", 0) );
var_dump( strripos($multi_line_strX, "inG", 20) );
var_dump( strripos($multi_line_strX, "Ing", 27) );

echo "\n-- Multi line strings with +ve offsets -- \n";
var_dump( strripos($multi_line_strX, "Ing", -1) );
var_dump( strripos($multi_line_strX, "Ing", -18) );
var_dump( strripos($multi_line_strX, "Ing", -55) );

echo "\n-- Multi line strings with no offset -- \n";
var_dump( strripos($multi_line_strX, "spAn") );
var_dump( strripos($multi_line_strX, "IPlE") );
var_dump( strripos($multi_line_strX, "") );
var_dump( strripos($multi_line_strX, " ") );

?>
===DONE===
--EXPECT--
*** Testing strripos() function: with heredoc strings ***
-- With heredoc string containing multi lines --

-- Multi line strings with +ve offsets -- 
int(44)
int(44)
int(44)

-- Multi line strings with +ve offsets -- 
int(44)
int(44)
bool(false)

-- Multi line strings with no offset -- 
int(18)
int(31)
bool(false)
int(55)
===DONE===
