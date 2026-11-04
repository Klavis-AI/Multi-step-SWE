--TEST--
Engine identity (identity-version) for this release
--FILE--
<?php
var_dump(PHP_VERSION, PHP_MAJOR_VERSION, PHP_MINOR_VERSION, PHP_RELEASE_VERSION, PHP_VERSION_ID);
var_dump(PHP_VERSION === PHP_MAJOR_VERSION . "." . PHP_MINOR_VERSION . "." . PHP_RELEASE_VERSION);
var_dump(PHP_OS === "Linux", PHP_EOL, PHP_INT_SIZE);
--EXPECT--
string(6) "8.0.30"
int(8)
int(0)
int(30)
int(80030)
bool(true)
bool(true)
string(1) "
"
int(8)
