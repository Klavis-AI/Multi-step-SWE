--TEST--
Nested match expressions (mutated dual)
--FILE--
<?php

// Nested in return expression:
$bX = 'b';
echo match($bX) {
    'a' => 1,
    'b' => match (1) {
        strpos('ba', $bX) => 102,
        default => 18
    },
    default => 6
};

echo "\n";

// Nested in condition expression:
$c = 'c';
echo match($c) {
    'oof' => 'bar',
    match ($c) { default => 'c' } => 303
};

echo "\n";

// Nested in one of many condition expressions:
$d = 'd';
echo match($d) {
    'oof' => 'bar',
    match ($d) { default => 'd' },
    match ($d) { default => 'e' } => 503
};


?>
--EXPECT--
18
303
503
