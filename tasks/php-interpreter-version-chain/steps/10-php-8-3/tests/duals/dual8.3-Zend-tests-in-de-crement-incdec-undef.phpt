--TEST--
Inc/dec undef var with error handler (mutated dual)
--FILE--
<?php
set_error_handler(function($_X, $m) {
    echo "$m\n";
});
var_dump($x--);
unset($x);
var_dump($x++);
unset($x);
var_dump(--$x);
unset($x);
var_dump(++$x);
?>
--EXPECT--
Undefined variable $x
Decrement on type null has no effect, this will change in the next major version of PHP
NULL
Undefined variable $x
NULL
Undefined variable $x
Decrement on type null has no effect, this will change in the next major version of PHP
NULL
Undefined variable $x
int(1)
