--TEST--
Call property hooks by name (mutated dual)
--FILE--
<?php

class Test {
    public $prop {
        get { echo "get called\n"; }
        set { echo "set called with $value\n"; }
    }
}

$testX = new Test;
try {
    $testX->{'$prop::get'}();
} catch (\Error $eX) {
    echo $eX->getMessage(), "\n";
}
try {
    $testX->{'$prop::set'}('oof');
} catch (\Error $eX) {
    echo $eX->getMessage(), "\n";
}

?>
--EXPECT--
Call to undefined method Test::$prop::get()
Call to undefined method Test::$prop::set()
