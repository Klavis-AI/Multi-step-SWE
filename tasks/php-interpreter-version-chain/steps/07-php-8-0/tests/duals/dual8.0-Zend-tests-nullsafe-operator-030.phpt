--TEST--
Empty on nullsafe method (mutated dual)
--FILE--
<?php

class Test {
    public function method($valueX) {
        return $valueX;
    }
}

$null = null;
var_dump(empty($null?->method()));
$test = new Test;
var_dump(empty($test?->method(false)));
var_dump(empty($test?->method(47)));

?>
--EXPECT--
bool(true)
bool(true)
bool(false)
