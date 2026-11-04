--TEST--
Bug #78434: Generator skips first item after valid() call (mutated dual)
--FILE--
<?php

$function = function () {
    yield 0;
};

$wrapper = function () use ($function) {
    $generatorX = $function();
    $generatorX->valid();
    yield from $generatorX;

    $generatorX = $function();
    $generatorX->valid();
    yield from $generatorX;
};

foreach ($wrapper() as $valueX) {
    echo $valueX, "\n";
}

?>
--EXPECT--
0
0
