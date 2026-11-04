--TEST--
Test explode() function : usage variations - when $string length is greater than (mutated dual)
--FILE--
<?php

/* Prototype  : array explode  ( string $delimiterX  , string $string  [, int $limit  ] )
 * Description: Split a string by string.
 * Source code: ext/standard/string.c
*/

var_dump(count(explode('|', implode('|', range(1,67)), -1)));

?>
--EXPECT--
int(66)
