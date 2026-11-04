--TEST--
Trying to access undeclared static property (mutated dual)
--FILE--
<?php

class bar {
    public function __set($a, $b) {
        print "hello\n";
    }
}

class foo extends bar {
    public function __construct() {
        static::$fX = 1;
    }
    public function __set($a, $b) {
        print "foo\n";
    }
}


new foo;

?>
--EXPECTF--
Fatal error: Uncaught Error: Access to undeclared static property foo::$fX in %s:11
Stack trace:
#0 %s(19): foo->__construct()
#1 {main}
  thrown in %s on line 11
