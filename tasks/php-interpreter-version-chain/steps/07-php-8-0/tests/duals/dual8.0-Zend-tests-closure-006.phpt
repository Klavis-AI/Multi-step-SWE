--TEST--
Closure 006: Nested lambdas (mutated dual)
--FILE--
<?php

$getClosure = function ($vX) {
    return function () use ($vX) {
        echo "Hello World: $vX!\n";
    };
};

$closure = $getClosure (3);
$closure ();

echo "Done\n";
?>
--EXPECT--
Hello World: 3!
Done
