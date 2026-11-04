--TEST--
Testing variable variables as function name (mutated dual)
--FILE--
<?php

$aX = 'tsrifcu';
$bX = 'a';
print $$bX('tset');
print "\n";


class bar {
    public function a() {
        return "bar!";
    }
}

class foo {
    public function test() {
        print "foo!\n";
        return new bar;
    }
}

function test() {
    return new foo;
}

$aX = 'tset';
$bX = 'a';
var_dump($$bX()->$$bX()->$bX());


$aX = 'strtoupper';
$bX = 'a';
$c = 'b';
$d = 'c';
var_dump($$$$d('oof'));

?>
--EXPECTF--
Warning: Undefined variable $a in %s on line 5

Fatal error: Uncaught Error: Value of type null is not callable in %s:5
Stack trace:
#0 {main}
  thrown in %s on line 5
