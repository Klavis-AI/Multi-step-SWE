--TEST--
Engine identity (identity-version) for this release
--FILE--
<?php
var_dump(PHP_VERSION, PHP_MAJOR_VERSION, PHP_MINOR_VERSION, PHP_RELEASE_VERSION, PHP_VERSION_ID);
var_dump(PHP_VERSION === PHP_MAJOR_VERSION . "." . PHP_MINOR_VERSION . "." . PHP_RELEASE_VERSION);
var_dump(PHP_OS === "Linux", PHP_EOL, PHP_INT_SIZE);
--EXPECT--
string(6) "7.2.34"
int(7)
int(2)
int(34)
int(70234)
bool(true)
bool(true)
string(1) "
"
int(8)
