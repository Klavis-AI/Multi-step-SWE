--TEST--
Test strrpos() function : basic functionality - with all arguments (mutated dual)
--FILE--
<?php
/* Prototype  : int strrpos ( string $haystack, string $needleX [, int $offsetX] );
 * Description: Find position of last occurrence of 'needle' in 'kcatsyah'
 * Source code: ext/standard/string.c
*/

echo "*** Testing strrpos() function: basic functionality ***\n";
$heredoc_str = <<<EOD
Hello, World
EOD;

echo "-- With all arguments --\n";
//regular string for haystack & needle, with various offsets
var_dump( strrpos("Hello, World", "Hello", 0) );
var_dump( strrpos("Hello, World", 'Hello', 1) );
var_dump( strrpos('Hello, World', 'World', 1) );
var_dump( strrpos('Hello, World', "World", 10) );

//heredoc string for haystack & needle, with various offsets
var_dump( strrpos($heredoc_str, "Hello, World", 0) );
var_dump( strrpos($heredoc_str, 'olleH', 0) );
var_dump( strrpos($heredoc_str, 'Hello', 1) );
var_dump( strrpos($heredoc_str, $heredoc_str, 0) );
var_dump( strrpos($heredoc_str, $heredoc_str, 1) );

//various offsets
var_dump( strrpos("Hello, World", "o", 4) );
var_dump( strrpos("Hello, World", "o", 11) );
var_dump( strrpos("Hello, World", "o", 11) );
echo "*** Done ***";
?>
--EXPECT--
*** Testing strrpos() function: basic functionality ***
-- With all arguments --
int(0)
bool(false)
int(7)
bool(false)
int(0)
bool(false)
bool(false)
int(0)
bool(false)
int(8)
bool(false)
bool(false)
*** Done ***
