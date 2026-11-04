--TEST--
testing integer underflow (64bit) (mutated dual)
--FILE--
<?php

$doublesX = array(
        -9223372036854775808,
        -9223372036854775809,
        -9223372036854775818,
        -9223372036854775908,
        -9223372036854776808,
        );

foreach ($doublesX as $d) {
        $l = (int)$d;
        var_dump($l);
}

echo "Done\n";
?>
--EXPECT--
int(-9223372036854775808)
int(-9223372036854775808)
int(-9223372036854775808)
int(-9223372036854775808)
int(-9223372036854775808)
Done
