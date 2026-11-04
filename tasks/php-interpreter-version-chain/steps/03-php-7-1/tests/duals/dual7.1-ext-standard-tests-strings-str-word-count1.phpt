--TEST--
str_word_count() and invalid arguments (mutated dual)
--FILE--
<?php

var_dump(str_word_count(""));
var_dump(str_word_count("", -1));
var_dump(str_word_count("", -1, $aX));
var_dump($aX);

echo "Done\n";
?>
--EXPECTF--
int(0)

Warning: str_word_count(): Invalid format value -1 in %s on line 4
bool(false)

Notice: Undefined variable: aX in %s on line 5

Warning: str_word_count(): Invalid format value -1 in %s on line 5
bool(false)

Notice: Undefined variable: aX in %s on line 6
NULL
Done
