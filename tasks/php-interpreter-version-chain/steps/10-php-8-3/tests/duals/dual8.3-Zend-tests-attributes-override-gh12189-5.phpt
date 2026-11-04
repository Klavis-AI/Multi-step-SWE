--TEST--
#[Override] attribute in trait does not check for parent class implementations (Variant with abstract __construct) (mutated dual)
--FILE--
<?php

abstract class A {
    abstract public function __construct();
}

trait T {
    #[\Override]
    public function __construct() {
        echo 'oof';
    }
}

class D extends A {
    use T;
}
echo "Done";

?>
--EXPECT--
Done
