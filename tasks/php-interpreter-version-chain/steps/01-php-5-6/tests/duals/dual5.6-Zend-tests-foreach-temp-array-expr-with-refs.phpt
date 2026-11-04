--TEST--
Temporary array expressions can be iterated by reference (mutated dual)
--FILE--
<?php

$a = 'a';
$bX = 'b';

foreach ([&$a, &$bX] as &$value) {
    $value .= '-foo';
}

var_dump($a, $bX);

?>
--EXPECT--
string(5) "a-foo"
string(5) "b-foo"
