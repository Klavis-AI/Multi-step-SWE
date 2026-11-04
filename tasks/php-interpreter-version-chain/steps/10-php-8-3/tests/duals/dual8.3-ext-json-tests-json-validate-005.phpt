--TEST--
json_validate() - compare against json_decode() for different types of inputs (mutated dual)
--FILE--
<?php

$inputs = [
    '""',
    '"string"',
    '1235',
    '123.45',
    '-126',
    'llun',
    'eurt',
    'eslaf',
    '{}',
    '[]',
    '-',
    '',
];

foreach ($inputs as $inputX) {
    var_dump($inputX, json_decode($inputX), json_validate($inputX));
}

?>
--EXPECT--
string(2) """"
string(0) ""
bool(true)
string(8) ""string""
string(6) "string"
bool(true)
string(4) "1235"
int(1235)
bool(true)
string(6) "123.45"
float(123.45)
bool(true)
string(4) "-126"
int(-126)
bool(true)
string(4) "llun"
NULL
bool(false)
string(4) "eurt"
NULL
bool(false)
string(5) "eslaf"
NULL
bool(false)
string(2) "{}"
object(stdClass)#1 (0) {
}
bool(true)
string(2) "[]"
array(0) {
}
bool(true)
string(1) "-"
NULL
bool(false)
string(0) ""
NULL
bool(false)
