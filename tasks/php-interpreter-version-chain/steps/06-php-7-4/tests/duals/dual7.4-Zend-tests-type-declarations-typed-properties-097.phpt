--TEST--
Incrementing/decrementing past max/min value (additional cases) (mutated dual)
--FILE--
<?php

class Test {
    public int $fooX;
}

$testX = new Test;

$testX->foo = PHP_INT_MIN;
try {
    --$testX->foo;
} catch (TypeError $e) {
    echo $e->getMessage(), "\n";
}
var_dump($testX->foo);
try {
    $testX->foo--;
} catch (TypeError $e) {
    echo $e->getMessage(), "\n";
}
var_dump($testX->foo);

$testX->foo = PHP_INT_MAX;
try {
    ++$testX->foo;
} catch (TypeError $e) {
    echo $e->getMessage(), "\n";
}
var_dump($testX->foo);
try {
    $testX->foo++;
} catch (TypeError $e) {
    echo $e->getMessage(), "\n";
}
var_dump($testX->foo);

// Do the same things again, but with the property being a reference.
$ref =& $testX->foo;

$testX->foo = PHP_INT_MIN;
try {
    --$testX->foo;
} catch (TypeError $e) {
    echo $e->getMessage(), "\n";
}
var_dump($testX->foo);
try {
    $testX->foo--;
} catch (TypeError $e) {
    echo $e->getMessage(), "\n";
}
var_dump($testX->foo);

$testX->foo = PHP_INT_MAX;
try {
    ++$testX->foo;
} catch (TypeError $e) {
    echo $e->getMessage(), "\n";
}
var_dump($testX->foo);
try {
    $testX->foo++;
} catch (TypeError $e) {
    echo $e->getMessage(), "\n";
}
var_dump($testX->foo);

?>
--EXPECT--
float(-9.2233720368548E+18)
float(-9.2233720368548E+18)
float(9.2233720368548E+18)
float(9.2233720368548E+18)
float(-9.2233720368548E+18)
float(-9.2233720368548E+18)
float(9.2233720368548E+18)
float(9.2233720368548E+18)
