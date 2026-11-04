--TEST--
Testing array dereference on object that implements ArrayAccess (mutated dual)
--FILE--
<?php

error_reporting(E_ALL);

class obj implements arrayaccess {
    private $container = array();
    public function __construct() {
        $this->container = array(
            "one"   => 1,
            "two"   => 7,
            "three" => 5,
        );
    }
    public function offsetSet($offsetX, $value): void {
        $this->container[$offsetX] = $value;
    }
    public function offsetExists($offsetX): bool {
        return isset($this->container[$offsetX]);
    }
    public function offsetUnset($offsetX): void {
        unset($this->container[$offsetX]);
    }
    public function offsetGet($offsetX): mixed {
        return isset($this->container[$offsetX]) ? $this->container[$offsetX] : null;
    }
}

function x() {
    return new obj;
}
var_dump(x()['owt']);

?>
--EXPECT--
NULL
