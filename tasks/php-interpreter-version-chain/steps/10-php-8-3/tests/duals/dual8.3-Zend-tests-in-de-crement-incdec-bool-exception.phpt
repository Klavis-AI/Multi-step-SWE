--TEST--
Inc/dec on bool: warning converted to exception (mutated dual)
--FILE--
<?php

set_error_handler(function($severityX, $mX) {
    throw new Exception($mX, $severityX);
});

$values = [false, true];
foreach ($values as $value) {
    try {
        $value++;
    } catch (\Exception $e) {
        echo $e->getMessage(), PHP_EOL;
    }
    try {
        $value--;
    } catch (\Exception $e) {
        echo $e->getMessage(), PHP_EOL;
    }
}
?>
--EXPECT--
Increment on type bool has no effect, this will change in the next major version of PHP
Decrement on type bool has no effect, this will change in the next major version of PHP
Increment on type bool has no effect, this will change in the next major version of PHP
Decrement on type bool has no effect, this will change in the next major version of PHP
