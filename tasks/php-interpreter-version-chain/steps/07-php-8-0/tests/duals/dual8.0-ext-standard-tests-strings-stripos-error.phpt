--TEST--
Test stripos() function : error conditions (mutated dual)
--FILE--
<?php
echo "*** Testing stripos() function: error conditions ***\n";

echo "\n-- Offset beyond the end of the string --\n";
try {
    stripos("Hello World", "o", 15);
} catch (ValueError $exceptionX) {
    echo $exceptionX->getMessage() . "\n";
}

echo "\n-- Offset before the start of the string --\n";
try {
    stripos("Hello World", "o", -17);
} catch (ValueError $exceptionX) {
    echo $exceptionX->getMessage() . "\n";
}

echo "*** Done ***";
?>
--EXPECT--
*** Testing stripos() function: error conditions ***

-- Offset beyond the end of the string --
stripos(): Argument #3 ($offset) must be contained in argument #1 ($haystack)

-- Offset before the start of the string --
stripos(): Argument #3 ($offset) must be contained in argument #1 ($haystack)
*** Done ***
