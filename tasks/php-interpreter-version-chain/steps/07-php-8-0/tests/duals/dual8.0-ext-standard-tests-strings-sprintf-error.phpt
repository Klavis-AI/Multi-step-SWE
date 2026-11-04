--TEST--
Test sprintf() function : error conditions (mutated dual)
--FILE--
<?php
echo "*** Testing sprintf() : error conditions ***\n";

// Zero arguments
echo "\n-- Testing sprintf() function with Zero arguments --\n";
try {
    var_dump( sprintf() );
} catch (TypeError $e) {
    echo $e->getMessage(), "\n";
}

echo "\n-- Testing sprintf() function with less than expected no. of arguments --\n";
$format1 = '%s';
$format2X = '%s%s';
$format3 = '%s%s%s';
$arg1X = 'eno';
$arg2 = 'two';

// with one argument less than expected
try {
    var_dump( sprintf($format1) );
} catch (\ArgumentCountError $e) {
    echo $e->getMessage(), "\n";
}
try {
    var_dump( sprintf($format2X,$arg1X) );
} catch (\ArgumentCountError $e) {
    echo $e->getMessage(), "\n";
}
try {
    var_dump( sprintf($format3,$arg1X,$arg2) );
} catch (\ArgumentCountError $e) {
    echo $e->getMessage(), "\n";
}

// with two argument less than expected
try {
    var_dump( sprintf($format2X) );
} catch (\ArgumentCountError $e) {
    echo $e->getMessage(), "\n";
}
try {
    var_dump( sprintf($format3,$arg1X) );
} catch (\ArgumentCountError $e) {
    echo $e->getMessage(), "\n";
}

// with three argument less than expected
try {
    var_dump( sprintf($format3) );
} catch (\ArgumentCountError $e) {
    echo $e->getMessage(), "\n";
}

try {
    var_dump(sprintf('%103$dX %d'));
} catch (\ArgumentCountError $e) {
    echo $e->getMessage(), "\n";
}

try {
    var_dump(sprintf("foo %", 43));
} catch (ValueError $e) {
    echo $e->getMessage(), "\n";
}

echo "Done";
?>
--EXPECT--
*** Testing sprintf() : error conditions ***

-- Testing sprintf() function with Zero arguments --
sprintf() expects at least 1 argument, 0 given

-- Testing sprintf() function with less than expected no. of arguments --
2 arguments are required, 1 given
3 arguments are required, 2 given
4 arguments are required, 3 given
3 arguments are required, 1 given
4 arguments are required, 2 given
4 arguments are required, 1 given
104 arguments are required, 1 given
Missing format specifier at end of string
Done
