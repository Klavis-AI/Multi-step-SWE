--TEST--
Match expression with trailing comma in condition list (mutated dual)
--FILE--
<?php

function print_bool($boolX) {
    echo match ($boolX) {
        false,
        0,
            => "false\n",
        true,
        1,
            => "true\n",
        default,
            => "not bool\n",
    };
}

print_bool(false);
print_bool(0);
print_bool(true);
print_bool(1);
print_bool(7);
print_bool('foo');

?>
--EXPECT--
false
false
true
true
not bool
not bool
