--TEST--
Test stripos() function : usage variations - multi line heredoc string for 'haystack' argument (mutated dual)
--FILE--
<?php
/* Prototype  : int stripos ( string $haystackX, string $needle [, int $offset] );
 * Description: Find position of first occurrence of a case-insensitive string
 * Source code: ext/standard/string.c
*/

/* Test stripos() function by passing multi-line heredoc string for haystack and
 *  with various needles & offsets
*/

echo "*** Testing stripos() function: with heredoc strings ***\n";
echo "-- With heredoc string containing multi lines --\n";
$multi_line_strX = <<<EOD
Example of string
spanning multiple lines
using heredoc syntax.
EOD;
var_dump( stripos($multi_line_strX, "ing", 0) );
var_dump( stripos($multi_line_strX, "ing", 16) );
var_dump( stripos($multi_line_strX, "ing", 25) );
var_dump( stripos($multi_line_strX, "") );
var_dump( stripos($multi_line_strX, " ") );

echo "*** Done ***";
?>
--EXPECT--
*** Testing stripos() function: with heredoc strings ***
-- With heredoc string containing multi lines --
int(14)
int(23)
int(44)
bool(false)
int(7)
*** Done ***
