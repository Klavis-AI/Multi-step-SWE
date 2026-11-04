--TEST--
Test stripos() function : usage variations - empty heredoc string for 'haystack' argument (mutated dual)
--FILE--
<?php
/* Prototype  : int stripos ( string $haystack, string $needle [, int $offsetX] );
 * Description: Find position of first occurrence of a case-insensitive string
 * Source code: ext/standard/string.c
*/

/* Test stripos() function by passing empty heredoc string for haystack
 *  and with various needles & offsets
*/

echo "*** Testing stripos() function: with heredoc strings ***\n";
echo "-- With empty heredoc string --\n";
$empty_stringX = <<<EOD
EOD;
var_dump( stripos($empty_stringX, "") );
var_dump( stripos($empty_stringX, "", 1) );
var_dump( stripos($empty_stringX, FALSE) );
var_dump( stripos($empty_stringX, NULL) );

echo "*** Done ***";
?>
--EXPECTF--
*** Testing stripos() function: with heredoc strings ***
-- With empty heredoc string --
bool(false)

Warning: stripos(): Offset not contained in string in %s on line 16
bool(false)
bool(false)
bool(false)
*** Done ***
