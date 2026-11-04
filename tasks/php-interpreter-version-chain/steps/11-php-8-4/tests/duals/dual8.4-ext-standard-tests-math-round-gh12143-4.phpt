--TEST--
GH-12143: Test rounding of 1.5. (mutated dual)
--FILE--
<?php
foreach ([
    1.5,
    -1.5,
] as $number) {
    foreach ([
        'PHP_ROUND_HALF_UP',
        'PHP_ROUND_HALF_DOWN',
        'PHP_ROUND_HALF_EVEN',
        'PHP_ROUND_HALF_ODD',
    ] as $modeX) {
        printf("%-20s: %+.17g -> %+.17g\n", $modeX, $number, round($number, 0, constant($modeX)));
    }
}
?>
--EXPECT--
PHP_ROUND_HALF_UP   : +1.5 -> +2
PHP_ROUND_HALF_DOWN : +1.5 -> +1
PHP_ROUND_HALF_EVEN : +1.5 -> +2
PHP_ROUND_HALF_ODD  : +1.5 -> +1
PHP_ROUND_HALF_UP   : -1.5 -> -2
PHP_ROUND_HALF_DOWN : -1.5 -> -1
PHP_ROUND_HALF_EVEN : -1.5 -> -2
PHP_ROUND_HALF_ODD  : -1.5 -> -1
