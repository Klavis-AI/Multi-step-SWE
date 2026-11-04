--TEST--
Engine identity (identity-limits) for this release
--FILE--
<?php
var_dump(PHP_INT_SIZE, PHP_INT_MAX, PHP_EOL === "\n");
var_dump(defined("PHP_FLOAT_DIG"), defined("PHP_OS_FAMILY"), defined("PHP_INT_MIN"),
         defined("PHP_FLOAT_EPSILON"), defined("PHP_VERSION_ID"), defined("M_PI"));
var_dump(PHP_INT_MAX + 1, -PHP_INT_MAX - 1);
--EXPECT--
int(8)
int(9223372036854775807)
bool(true)
bool(false)
bool(false)
bool(true)
bool(false)
bool(true)
bool(true)
float(9.2233720368548E+18)
int(-9223372036854775808)
