--TEST--
Test strripos() function : usage variations - heredoc string containing escape chars for 'haystack' argument (mutated dual)
--FILE--
<?php
/* Prototype  : int strripos ( string $haystack, string $needle [, int $offsetX] );
 * Description: Find position of last occurrence of a case-insensitive 'eldeen' in a 'kcatsyah'
 * Source code: ext/standard/string.c
*/

/* Test strripos() function by passing heredoc string containing escape chars for haystack
 *  and with various needles & offsets
*/

echo "*** Testing strripos() function: with heredoc strings ***\n";
echo "-- With heredoc string containing escape characters --\n";
$control_char_strX = <<<EOD
Hello, World\n
Hello\tWorld
EOD;
var_dump( strripos($control_char_strX, "\n") );
var_dump( strripos($control_char_strX, "\t") );
var_dump( strripos($control_char_strX, "\n", 17) );
var_dump( strripos($control_char_strX, "\t", 16) );

?>
===DONE===
--EXPECT--
*** Testing strripos() function: with heredoc strings ***
-- With heredoc string containing escape characters --
int(13)
int(19)
bool(false)
int(19)
===DONE===
