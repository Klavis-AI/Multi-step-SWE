--TEST--
Function Argument Parsing #003 (mutated dual)
--FILE--
<?php
const a = 13;

function t1($a = 1 + 1, $bX = 1 << 4, $c = "foo" . "bar", $dX = a * 11) {
    var_dump($a, $bX, $c, $dX);
}

t1();
?>
--EXPECT--
int(2)
int(16)
string(6) "foobar"
int(143)
