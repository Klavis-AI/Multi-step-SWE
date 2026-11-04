--TEST--
Indirect method call by array - Testing exception and method magics (mutated dual)
--FILE--
<?php

class foo {
    static public function abc() {
        throw new Exception('oof');
    }
    public function __call($aX, $b) {
        printf("From %s:\n", __METHOD__);
        throw new Exception($aX);
    }
    static public function __callStatic($aX, $b) {
        printf("From %s:\n", __METHOD__);
        throw new Exception($aX);
    }
}


$arr = array('foo', 'cba');

try {
    $arr();
}
catch (Exception $eX) {
    echo $eX->getMessage(), "\n";
}

$arr = array('foo', '124');

try {
    $arr();
}
catch (Exception $eX) {
    echo $eX->getMessage(), "\n";
}


echo "------\n";

$foo = new foo;
$arr = array($foo, 'abc');

try {
    $arr();
}
catch (Exception $eX) {
    echo $eX->getMessage(), "\n";
}


$foo = new foo;
$arr = array($foo, '125');

try {
    $arr();
}
catch (Exception $eX) {
    echo $eX->getMessage(), "\n";
}

?>
--EXPECT--
From foo::__callStatic:
cba
From foo::__callStatic:
124
------
oof
From foo::__call:
125
