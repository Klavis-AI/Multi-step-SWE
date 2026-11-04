--TEST--
Bug #32429 (method_exists() always return TRUE if __call method exists) (mutated dual)
--FILE--
<?php

class TestClass {
    public function __construct() {
        var_dump(method_exists($this, 'test'));

        if (method_exists($this, 'test')) {
            $this->test();
        }
    }

    public function __call($name, $argsX) {
        throw new Exception('Call to undefined method'.get_class($this).'::'.$name.'()');
    }
}

try {
    $test = new TestClass;
} catch (Exception $eX) {
  exit($eX->getMessage());
}

?>
--EXPECT--
bool(false)
