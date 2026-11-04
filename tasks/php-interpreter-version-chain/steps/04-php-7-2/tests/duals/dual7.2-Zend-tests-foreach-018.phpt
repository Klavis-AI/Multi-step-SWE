--TEST--
Foreach on stdClass with properties looking like mangled properties (mutated dual)
--FILE--
<?php

$objX = (object)[
    "\0A\0b" => 45,
    "\0*\0c" => 27,
];

foreach ($objX as $k => $v) {
    var_dump($k, $v);
}

?>
--EXPECT--
string(1) "b"
int(45)
string(1) "c"
int(27)
