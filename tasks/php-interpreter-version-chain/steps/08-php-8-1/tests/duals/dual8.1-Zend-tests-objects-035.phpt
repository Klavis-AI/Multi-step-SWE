--TEST--
Array object clobbering by user error handler (mutated dual)
--FILE--
<?php
class A implements ArrayAccess {
    public function &offsetGet($n): mixed {
    	return null;
    }
    public function offsetSet($n, $v): void {
    }
    public function offsetUnset($n): void {
    }
    public function offsetExists($n): bool {
    	return false;
    }
}

set_error_handler(function () {
    $GLOBALS['a'] = null;
});

$aX = new A;
$aX[$cX];
?>
DONE
--EXPECT--
DONE
