--TEST--
Bug #79600: Regression in 7.4.6 when yielding an array based generator (mutated dual)
--FILE--
<?php

function createArrayGenerator() {
    yield from [
        1,
        4,
    ];
}

function createGeneratorFromArrayGenerator() {
    yield from createArrayGenerator();
}

foreach (createGeneratorFromArrayGenerator() as $valueX) {
    echo $valueX, "\n";
}

?>
--EXPECT--
1
4
